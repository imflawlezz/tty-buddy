//! Session supervisor: wait for device → run → on disconnect, retry forever.

use std::io::IsTerminal;
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

use anyhow::{Context, Result};
use crossterm::event::{self, Event, KeyCode, KeyModifiers};
use crossterm::terminal::{disable_raw_mode, enable_raw_mode};

use crate::discover::resolve_device;
use crate::keyboard::Keyboard;
use crate::metrics::MetricsCollector;
use crate::protocol::{
    FLAG_ACTIVITY, FLAG_BYE, FLAG_CURSOR_ON, FLAG_CURSOR_VISIBLE, FLAG_STATUS, FLAG_STYLE,
    PAYLOAD_LEN,
};
use crate::serial_io::{BuddySerial, DeviceEvent};
use crate::settings::DaemonSettings;
use crate::status_config::{
    apply_daemon_behavior_fallback, apply_osd_levels, load_status_config, maybe_reload,
    write_osd_levels, StatusUiConfig,
};
use crate::terminal::PtySession;

pub fn run_forever(
    settings: &DaemonSettings,
    buddy_config: &Path,
    fps_override: Option<f32>,
    force_status: Option<bool>,
) -> Result<()> {
    let running = Arc::new(AtomicBool::new(true));
    {
        let r = running.clone();
        ctrlc::set_handler(move || r.store(false, Ordering::SeqCst)).ok();
    }

    eprintln!("tty-buddy daemon starting (auto-reconnect)");
    while running.load(Ordering::SeqCst) {
        if let Ok(dev) = resolve_device(settings) {
            eprintln!("device online: {dev}");
            if let Err(e) = run_session(
                &dev,
                settings,
                buddy_config,
                fps_override,
                force_status,
                &running,
            ) {
                eprintln!("session ended: {e:#}");
            }
            eprintln!("waiting for device…");
        }
        if !running.load(Ordering::SeqCst) {
            break;
        }
        std::thread::sleep(Duration::from_millis(400));
    }
    Ok(())
}

fn device_still_there(path: &str, settings: &DaemonSettings) -> bool {
    Path::new(path).exists()
        || Path::new("/dev/tty-buddy").exists()
        || resolve_device(settings).is_ok()
}

fn ensure_pty(pty: &mut Option<PtySession>, settings: &DaemonSettings) -> Result<()> {
    if pty.is_none() {
        let user = settings.shell_user.as_deref();
        *pty = Some(PtySession::spawn_login(user).context("spawn console pty")?);
    }
    Ok(())
}

fn enter_terminal(
    pty: &mut Option<PtySession>,
    keyboard: &mut Keyboard,
    settings: &DaemonSettings,
) -> Result<()> {
    ensure_pty(pty, settings)?;
    if let Some(session) = pty.as_mut() {
        session.force_resize();
    }
    keyboard.grab()?;
    Ok(())
}

fn enter_status(keyboard: &mut Keyboard, watch_for_terminal: bool) {
    if watch_for_terminal {
        let _ = keyboard.watch();
    } else {
        keyboard.ungrab();
    }
}

fn push_style(serial: &mut BuddySerial, cfg: &StatusUiConfig, metrics: &mut MetricsCollector) {
    match metrics.sample(cfg) {
        Ok(snap) => {
            if let Err(e) = serial.send_frame(0, 0, FLAG_STYLE, &snap.to_payload()) {
                eprintln!("style push: {e:#}");
            }
        }
        Err(e) => eprintln!("style push sample: {e:#}"),
    }
}

fn apply_device_osd(path: &Path, cfg: &mut StatusUiConfig, bright: Option<u8>, sleep: Option<u8>) {
    let b = bright.unwrap_or(cfg.style.osd_default_bright_pct);
    let s = sleep.unwrap_or(cfg.style.osd_sleep_timeout_s.min(6) as u8);
    apply_osd_levels(cfg, b, s);
    if let Err(e) = write_osd_levels(path, b, s) {
        eprintln!("osd config write: {e:#}");
        return;
    }
    if let Ok(mtime) = std::fs::metadata(path).and_then(|m| m.modified()) {
        cfg.mtime = Some(mtime);
    }
    eprintln!(
        "osd → config: brightness={} sleep={}",
        if b == 0 {
            "auto".to_string()
        } else {
            b.to_string()
        },
        if s == 0 {
            "never".to_string()
        } else {
            s.to_string()
        }
    );
}

pub fn run_session(
    device: &str,
    settings: &DaemonSettings,
    buddy_config: &Path,
    fps_override: Option<f32>,
    force_status: Option<bool>,
    running: &AtomicBool,
) -> Result<()> {
    let mut serial = BuddySerial::open(device)?;
    let mut cfg = load_status_config(buddy_config)
        .with_context(|| format!("load {}", buddy_config.display()))?;
    apply_daemon_behavior_fallback(&mut cfg, settings);
    if let Some(f) = fps_override {
        cfg.fps = f.max(1.0);
    }
    if let Some(s) = force_status {
        cfg.startup_status = s;
    }
    let mut metrics = MetricsCollector::new();
    let _ = metrics.sample(&cfg)?;
    std::thread::sleep(Duration::from_millis(200));
    push_style(&mut serial, &cfg, &mut metrics);

    let mut status_mode = cfg.startup_status;
    let mut frame_dt = Duration::from_secs_f32(1.0 / cfg.fps.max(1.0));
    let status_dt = Duration::from_secs(1);
    let mut last_term = Instant::now() - frame_dt;
    let mut last_status = Instant::now() - status_dt;
    let mut cursor_on = true;
    let mut last_blink = Instant::now();
    let mut consecutive_fail = 0u32;
    let mut last_kb_scan = Instant::now() - Duration::from_secs(2);

    let mut pty: Option<PtySession> = None;
    let mut keyboard = Keyboard::open().unwrap_or_else(|e| {
        eprintln!("keyboard: {e}");
        Keyboard::disabled()
    });

    let interactive = std::io::stdin().is_terminal();
    if interactive && !status_mode {
        enable_raw_mode().ok();
    }

    if !status_mode {
        enter_terminal(&mut pty, &mut keyboard, settings)?;
    } else {
        enter_status(&mut keyboard, cfg.keyboard_opens_terminal);
    }

    eprintln!(
        "session live — mode={}",
        if status_mode { "status" } else { "terminal" }
    );

    while running.load(Ordering::SeqCst) {
        if !device_still_there(device, settings) {
            anyhow::bail!("device unplugged");
        }

        if let Some(mut new_cfg) = maybe_reload(buddy_config, &cfg) {
            apply_daemon_behavior_fallback(&mut new_cfg, settings);
            if let Some(f) = fps_override {
                new_cfg.fps = f.max(1.0);
            }
            let kb_changed = new_cfg.keyboard_opens_terminal != cfg.keyboard_opens_terminal;
            cfg = new_cfg;
            frame_dt = Duration::from_secs_f32(1.0 / cfg.fps.max(1.0));
            eprintln!("reloaded {}", buddy_config.display());
            push_style(&mut serial, &cfg, &mut metrics);
            if status_mode && kb_changed {
                enter_status(&mut keyboard, cfg.keyboard_opens_terminal);
            }
        }

        for ev in serial.poll_events() {
            match ev {
                DeviceEvent::ModeToggle => {
                    status_mode = !status_mode;
                    eprintln!("mode → {}", if status_mode { "status" } else { "terminal" });
                    if status_mode {
                        enter_status(&mut keyboard, cfg.keyboard_opens_terminal);
                    } else {
                        enter_terminal(&mut pty, &mut keyboard, settings)?;
                    }
                }
                DeviceEvent::Brightness(v) => {
                    apply_device_osd(buddy_config, &mut cfg, Some(v), None);
                }
                DeviceEvent::Sleep(v) => {
                    apply_device_osd(buddy_config, &mut cfg, None, Some(v));
                }
            }
        }

        // Drain PTY even in status mode so TUIs do not block on a full output buffer.
        if let Some(session) = pty.as_mut() {
            for _ in 0..64 {
                let _ = session.pump();
            }
            if session.try_wait() {
                eprintln!("shell exited — respawning");
                let user = settings.shell_user.as_deref();
                *session = PtySession::spawn_login(user)?;
                if !status_mode {
                    keyboard.grab().ok();
                }
            }
        }

        if last_blink.elapsed() >= Duration::from_millis(500) {
            cursor_on = !cursor_on;
            last_blink = Instant::now();
        }

        let send_result = if status_mode {
            if cfg.keyboard_opens_terminal {
                let scan_due = last_kb_scan.elapsed() >= Duration::from_millis(750)
                    || keyboard.device_count() == 0;
                if scan_due {
                    let _ = keyboard.maintain();
                    last_kb_scan = Instant::now();
                }
            }

            let pending = if cfg.keyboard_opens_terminal {
                keyboard.poll()
            } else {
                Vec::new()
            };
            if !pending.is_empty() {
                status_mode = false;
                eprintln!("mode → terminal (keyboard)");
                enter_terminal(&mut pty, &mut keyboard, settings)?;
                ensure_pty(&mut pty, settings)?;
                let session = pty.as_mut().unwrap();
                let app_cursor = session.application_cursor();
                for key in pending {
                    let bytes = if app_cursor {
                        map_app_cursor(&key).unwrap_or(key)
                    } else {
                        key
                    };
                    let _ = session.write_input(&bytes);
                }
                let frame = session.frame();
                let guard = frame.lock().unwrap();
                let payload = guard.payload;
                let (cx, cy) = guard.cursor;
                let hide = guard.hide_cursor;
                drop(guard);
                let mut flags = FLAG_ACTIVITY;
                if !hide {
                    flags |= FLAG_CURSOR_VISIBLE;
                    if cursor_on {
                        flags |= FLAG_CURSOR_ON;
                    }
                }
                last_term = Instant::now();
                serial.send_frame(cx, cy, flags, &payload)
            } else if last_status.elapsed() >= status_dt {
                let snap = metrics.sample(&cfg)?;
                let payload = snap.to_payload();
                last_status = Instant::now();
                serial.send_frame(0, 0, FLAG_STATUS, &payload)
            } else {
                Ok(())
            }
        } else {
            ensure_pty(&mut pty, settings)?;
            let session = pty.as_mut().unwrap();

            let scan_due = last_kb_scan.elapsed() >= Duration::from_millis(750)
                || keyboard.grabbed_count() == 0;
            if scan_due {
                let _ = keyboard.maintain();
                last_kb_scan = Instant::now();
            }

            let mut input_activity = false;
            let app_cursor = session.application_cursor();
            for key in keyboard.poll() {
                input_activity = true;
                let bytes = if app_cursor {
                    map_app_cursor(&key).unwrap_or(key)
                } else {
                    key
                };
                let _ = session.write_input(&bytes);
            }

            if interactive {
                while event::poll(Duration::from_millis(0)).unwrap_or(false) {
                    if let Ok(Event::Key(key)) = event::read() {
                        if key.code == KeyCode::Char('c')
                            && key.modifiers.contains(KeyModifiers::CONTROL)
                        {
                            running.store(false, Ordering::SeqCst);
                            break;
                        }
                        let mut bytes = Vec::new();
                        match key.code {
                            KeyCode::Char(c) => {
                                if key.modifiers.contains(KeyModifiers::CONTROL) {
                                    bytes.push((c.to_ascii_lowercase() as u8) & 0x1f);
                                } else {
                                    bytes.push(c as u8);
                                }
                            }
                            KeyCode::Enter => bytes.push(b'\r'),
                            KeyCode::Backspace => bytes.push(0x7f),
                            KeyCode::Tab => bytes.push(b'\t'),
                            KeyCode::Esc => bytes.push(0x1b),
                            KeyCode::Up => bytes.extend_from_slice(if app_cursor {
                                b"\x1bOA"
                            } else {
                                b"\x1b[A"
                            }),
                            KeyCode::Down => bytes.extend_from_slice(if app_cursor {
                                b"\x1bOB"
                            } else {
                                b"\x1b[B"
                            }),
                            KeyCode::Right => bytes.extend_from_slice(if app_cursor {
                                b"\x1bOC"
                            } else {
                                b"\x1b[C"
                            }),
                            KeyCode::Left => bytes.extend_from_slice(if app_cursor {
                                b"\x1bOD"
                            } else {
                                b"\x1b[D"
                            }),
                            _ => {}
                        }
                        if !bytes.is_empty() {
                            input_activity = true;
                            let _ = session.write_input(&bytes);
                        }
                    }
                }
            }

            if last_term.elapsed() >= frame_dt || input_activity {
                let frame = session.frame();
                let guard = frame.lock().unwrap();
                let payload = guard.payload;
                let (cx, cy) = guard.cursor;
                let hide = guard.hide_cursor;
                drop(guard);
                let mut flags = 0u8;
                if !hide {
                    flags |= FLAG_CURSOR_VISIBLE;
                    if cursor_on {
                        flags |= FLAG_CURSOR_ON;
                    }
                }
                if input_activity {
                    flags |= FLAG_ACTIVITY;
                }
                last_term = Instant::now();
                serial.send_frame(cx, cy, flags, &payload)
            } else {
                Ok(())
            }
        };

        match send_result {
            Ok(()) => consecutive_fail = 0,
            Err(e) => {
                consecutive_fail += 1;
                eprintln!("serial I/O: {e:#} ({consecutive_fail})");
                if consecutive_fail >= 3 {
                    anyhow::bail!("serial link lost");
                }
                std::thread::sleep(Duration::from_millis(150));
            }
        }

        std::thread::sleep(Duration::from_millis(5));
    }

    let mut bye = [0u8; PAYLOAD_LEN];
    for i in 0..(PAYLOAD_LEN / 3) {
        bye[i * 3 + 1] = b' ';
        bye[i * 3 + 2] = 0x07;
    }
    let _ = serial.send_frame(0, 0, FLAG_BYE, &bye);
    keyboard.ungrab();
    disable_raw_mode().ok();
    Ok(())
}

/// CSI arrows → SS3 when the PTY is in application-cursor mode.
fn map_app_cursor(key: &[u8]) -> Option<Vec<u8>> {
    match key {
        b"\x1b[A" => Some(b"\x1bOA".to_vec()),
        b"\x1b[B" => Some(b"\x1bOB".to_vec()),
        b"\x1b[C" => Some(b"\x1bOC".to_vec()),
        b"\x1b[D" => Some(b"\x1bOD".to_vec()),
        _ => None,
    }
}
