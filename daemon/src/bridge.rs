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
    FLAG_BYE, FLAG_CURSOR_ON, FLAG_CURSOR_VISIBLE, FLAG_STATUS, PAYLOAD_LEN,
};
use crate::serial_io::BuddySerial;
use crate::settings::DaemonSettings;
use crate::status_config::{load_status_config, maybe_reload};
use crate::terminal::PtySession;

pub fn run_forever(settings: &DaemonSettings, status_config: &Path, fps: f32) -> Result<()> {
    let running = Arc::new(AtomicBool::new(true));
    {
        let r = running.clone();
        ctrlc::set_handler(move || r.store(false, Ordering::SeqCst)).ok();
    }

    eprintln!("tty-buddy daemon starting (auto-reconnect)");
    while running.load(Ordering::SeqCst) {
        match resolve_device(settings) {
            Ok(dev) => {
                eprintln!("device online: {dev}");
                if let Err(e) = run_session(&dev, settings, status_config, fps, &running) {
                    eprintln!("session ended: {e:#}");
                }
                eprintln!("waiting for device…");
            }
            Err(_) => {}
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

fn enter_status(keyboard: &mut Keyboard) {
    keyboard.ungrab();
}

pub fn run_session(
    device: &str,
    settings: &DaemonSettings,
    status_config: &Path,
    fps: f32,
    running: &AtomicBool,
) -> Result<()> {
    let mut serial = BuddySerial::open(device)?;
    let mut cfg = load_status_config(status_config)
        .with_context(|| format!("load {}", status_config.display()))?;
    let mut metrics = MetricsCollector::new();
    let _ = metrics.sample(&cfg)?;
    std::thread::sleep(Duration::from_millis(200));

    let mut status_mode = settings.start_in_status;
    let frame_dt = Duration::from_secs_f32(1.0 / fps.max(1.0));
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
    }

    eprintln!(
        "session live — mode={}",
        if status_mode { "status" } else { "terminal" }
    );

    while running.load(Ordering::SeqCst) {
        if !device_still_there(device, settings) {
            anyhow::bail!("device unplugged");
        }

        if let Some(new_cfg) = maybe_reload(status_config, &cfg) {
            cfg = new_cfg;
            eprintln!("reloaded {}", status_config.display());
        }

        if serial.poll_toggle() {
            status_mode = !status_mode;
            eprintln!(
                "mode → {}",
                if status_mode { "status" } else { "terminal" }
            );
            if status_mode {
                enter_status(&mut keyboard);
            } else {
                enter_terminal(&mut pty, &mut keyboard, settings)?;
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
            if last_status.elapsed() >= status_dt {
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

            let app_cursor = session.application_cursor();
            for key in keyboard.poll() {
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
                            let _ = session.write_input(&bytes);
                        }
                    }
                }
            }

            if last_term.elapsed() >= frame_dt {
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
