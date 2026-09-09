use std::fs;
use std::path::PathBuf;

use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DaemonSettings {
    /// Preferred device node, e.g. /dev/ttyACM0 or /dev/tty-buddy
    pub device_path: Option<String>,
    pub vid: Option<u16>,
    pub pid: Option<u16>,
    pub serial: Option<String>,
    /// Path to status.config (INI)
    pub status_config: Option<PathBuf>,
    /// Console shell user (must match the systemd service user)
    pub shell_user: Option<String>,
    /// Start in status GUI (long-press toggles console)
    #[serde(default = "default_true")]
    pub start_in_status: bool,
    #[serde(default = "default_fps")]
    pub fps: f32,
}

fn default_true() -> bool {
    true
}
fn default_fps() -> f32 {
    10.0
}

impl Default for DaemonSettings {
    fn default() -> Self {
        Self {
            device_path: None,
            vid: Some(0x303A),
            pid: Some(0x1001),
            serial: None,
            status_config: Some(PathBuf::from("/etc/tty-buddy/status.config")),
            shell_user: None,
            start_in_status: true,
            fps: 10.0,
        }
    }
}

impl DaemonSettings {
    pub fn load_or_default(path: &PathBuf) -> Self {
        match fs::read_to_string(path) {
            Ok(s) => toml::from_str(&s).unwrap_or_default(),
            Err(_) => Self::default(),
        }
    }

    pub fn save(&self, path: &PathBuf) -> Result<()> {
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).ok();
        }
        let s = toml::to_string_pretty(self).context("serialize settings")?;
        fs::write(path, s).with_context(|| format!("write {}", path.display()))?;
        Ok(())
    }
}

/// `./daemon.toml`, then `/etc/tty-buddy/daemon.toml`, then XDG config.
pub fn settings_path() -> PathBuf {
    for p in [
        PathBuf::from("daemon.toml"),
        PathBuf::from("/etc/tty-buddy/daemon.toml"),
    ] {
        if p.exists() {
            return p;
        }
    }
    if let Some(dir) = dirs::config_dir() {
        return dir.join("tty-buddy").join("daemon.toml");
    }
    PathBuf::from("/etc/tty-buddy/daemon.toml")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults() {
        let s = DaemonSettings::default();
        assert_eq!(s.vid, Some(0x303A));
        assert_eq!(s.pid, Some(0x1001));
        assert!(s.start_in_status);
        assert_eq!(s.fps, 10.0);
        assert_eq!(
            s.status_config.as_deref(),
            Some(std::path::Path::new("/etc/tty-buddy/status.config"))
        );
    }

    #[test]
    fn load_missing_uses_defaults() {
        let path = PathBuf::from("/no/such/daemon.toml");
        let s = DaemonSettings::load_or_default(&path);
        assert_eq!(s.vid, Some(0x303A));
        assert_eq!(s.fps, 10.0);
    }

    #[test]
    fn round_trip_toml() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("daemon.toml");
        let s = DaemonSettings {
            device_path: Some("/dev/tty-buddy".into()),
            fps: 12.5,
            start_in_status: false,
            shell_user: Some("alice".into()),
            ..Default::default()
        };
        s.save(&path).unwrap();

        let loaded = DaemonSettings::load_or_default(&path);
        assert_eq!(loaded.device_path.as_deref(), Some("/dev/tty-buddy"));
        assert_eq!(loaded.fps, 12.5);
        assert!(!loaded.start_in_status);
        assert_eq!(loaded.shell_user.as_deref(), Some("alice"));
        assert_eq!(loaded.vid, Some(0x303A));
    }

    #[test]
    fn invalid_toml_falls_back_to_default() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("bad.toml");
        fs::write(&path, "[[[not valid").unwrap();
        let s = DaemonSettings::load_or_default(&path);
        assert_eq!(s.fps, 10.0);
        assert!(s.start_in_status);
    }
}
