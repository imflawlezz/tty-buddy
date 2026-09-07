//! Host daemon for the ESP32 status display and console PTY.

mod bridge;
mod discover;
mod keyboard;
mod metrics;
mod protocol;
mod serial_io;
mod settings;
mod status_config;
mod terminal;

use std::path::PathBuf;

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};

use crate::discover::{list_serial_devices, resolve_device};
use crate::settings::{settings_path, DaemonSettings};

#[derive(Parser, Debug)]
#[command(name = "tty-buddy", about = "ESP32 tty-buddy host daemon")]
struct Cli {
    #[command(subcommand)]
    cmd: Commands,
}

#[derive(Subcommand, Debug)]
enum Commands {
    /// Interactive device picker; writes daemon.toml
    Setup {
        #[arg(long)]
        config: Option<PathBuf>,
    },
    /// Run as daemon (reconnect forever). Used by systemd.
    Run {
        #[arg(long)]
        config: Option<PathBuf>,
        #[arg(short, long)]
        port: Option<String>,
        #[arg(long)]
        status_config: Option<PathBuf>,
        #[arg(long)]
        fps: Option<f32>,
        /// Force start in status mode
        #[arg(long)]
        status: bool,
        /// Force start in terminal/console mode
        #[arg(long)]
        terminal: bool,
    },
    /// List USB serial devices
    Devices,
    /// Sample live host metrics (no serial)
    Probe {
        #[arg(long)]
        status_config: Option<PathBuf>,
    },
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    match cli.cmd {
        Commands::Devices => {
            for d in list_serial_devices()? {
                println!("{d}");
            }
            Ok(())
        }
        Commands::Probe { status_config } => cmd_probe(status_config),
        Commands::Setup { config } => cmd_setup(config),
        Commands::Run {
            config,
            port,
            status_config,
            fps,
            status,
            terminal,
        } => cmd_run(config, port, status_config, fps, status, terminal),
    }
}

fn cmd_setup(config: Option<PathBuf>) -> Result<()> {
    use std::io::{Write, stdin, stdout};
    let path = config.unwrap_or_else(settings_path);
    let devices = list_serial_devices()?;
    if devices.is_empty() {
        anyhow::bail!("no serial devices found — plug in the ESP first");
    }

    println!("tty-buddy setup — pick serial device\n");
    for (i, d) in devices.iter().enumerate() {
        println!("  [{i}] {d}");
    }
    print!("\nNumber [0]: ");
    stdout().flush()?;
    let mut line = String::new();
    stdin().read_line(&mut line)?;
    let idx: usize = {
        let t = line.trim();
        if t.is_empty() {
            0
        } else {
            t.parse().context("invalid selection")?
        }
    };
    let chosen = devices.get(idx).context("selection out of range")?;

    print!("Linux login user for on-device console [{}]: ", whoami());
    stdout().flush()?;
    line.clear();
    stdin().read_line(&mut line)?;
    let user = {
        let t = line.trim();
        if t.is_empty() {
            whoami()
        } else {
            t.to_string()
        }
    };

    let mut settings = DaemonSettings::load_or_default(&path);
    settings.device_path = Some(chosen.path.clone());
    settings.vid = chosen.vid;
    settings.pid = chosen.pid;
    settings.serial = chosen.serial.clone();
    settings.shell_user = Some(user.clone());
    settings.start_in_status = true;
    if settings.status_config.is_none() {
        settings.status_config = Some(PathBuf::from("/etc/tty-buddy/status.config"));
    }
    settings.save(&path)?;
    println!("\nSaved {}", path.display());
    println!("  device = {}", chosen.path);
    println!("  user   = {user}");
    println!("Enable:  sudo systemctl enable --now tty-buddy");
    Ok(())
}

fn whoami() -> String {
    std::env::var("USER").unwrap_or_else(|_| "root".into())
}

fn cmd_run(
    config: Option<PathBuf>,
    port: Option<String>,
    status_config: Option<PathBuf>,
    fps: Option<f32>,
    status: bool,
    terminal: bool,
) -> Result<()> {
    let path = config.unwrap_or_else(settings_path);
    let mut settings = DaemonSettings::load_or_default(&path);
    if let Some(p) = port {
        settings.device_path = Some(p);
    }
    if let Some(sc) = status_config {
        settings.status_config = Some(sc);
    }
    if let Some(f) = fps {
        settings.fps = f;
    }
    if status {
        settings.start_in_status = true;
    }
    if terminal {
        settings.start_in_status = false;
    }

    let status_path = settings
        .status_config
        .clone()
        .unwrap_or_else(|| PathBuf::from("/etc/tty-buddy/status.config"));

    let status_path = if status_path.exists() {
        status_path
    } else if PathBuf::from("status.config").exists() {
        PathBuf::from("status.config")
    } else {
        status_path
    };

    bridge::run_forever(&settings, &status_path, settings.fps)
}

fn cmd_probe(status_config: Option<PathBuf>) -> Result<()> {
    use crate::metrics::MetricsCollector;
    use crate::protocol::STATUS_SNAP_LEN;
    use crate::status_config::load_status_config;

    let path = status_config.unwrap_or_else(|| {
        if PathBuf::from("status.config").exists() {
            PathBuf::from("status.config")
        } else {
            PathBuf::from("/etc/tty-buddy/status.config")
        }
    });
    let cfg = load_status_config(&path)?;
    let mut m = MetricsCollector::new();
    let _ = m.sample(&cfg)?;
    std::thread::sleep(std::time::Duration::from_millis(250));
    let snap = m.sample(&cfg)?;
    let packed = snap.pack();
    println!("hostname     {}", snap.hostname);
    println!("date/time    {} {}", snap.date, snap.time);
    println!(
        "cpu/mem/disk {}% / {}% / {}%",
        snap.cpu_pct, snap.mem_pct, snap.disk_pct
    );
    println!(
        "load         {:.2} {:.2} {:.2}",
        snap.load_x100[0] as f32 / 100.0,
        snap.load_x100[1] as f32 / 100.0,
        snap.load_x100[2] as f32 / 100.0
    );
    println!("uptime_sec   {}", snap.uptime_sec);
    println!("mem          {}/{} MB", snap.mem_used_mb, snap.mem_total_mb);
    println!(
        "disk ({})  {}/{} MB",
        cfg.disk_mount, snap.disk_used_mb, snap.disk_total_mb
    );
    if snap.cpu_temp_c10 != 0x7FFF {
        println!("cpu_temp     {:.1} C", snap.cpu_temp_c10 as f32 / 10.0);
    }
    println!("ifaces       {}", snap.ifaces.len());
    for i in &snap.ifaces {
        println!("  {}  {}  rx={} tx={}", i.name, i.ip, i.rx_bps, i.tx_bps);
    }
    println!("services     {}", snap.services.len());
    for s in snap.services.iter().take(20) {
        println!("  {} status={}", s.name, s.status);
    }
    println!("snap_bytes   {} (expect {})", packed.len(), STATUS_SNAP_LEN);
    assert_eq!(packed.len(), STATUS_SNAP_LEN);
    let _ = resolve_device;
    println!("ok");
    Ok(())
}
