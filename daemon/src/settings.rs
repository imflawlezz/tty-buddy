use std::fs;
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DaemonSettings {
    pub device_path: Option<String>,
    pub vid: Option<u16>,
    pub pid: Option<u16>,
    pub serial: Option<String>,
    /// Panel UX config. Legacy TOML key: `status_config`.
    #[serde(default, alias = "status_config")]
    pub buddy_config: Option<PathBuf>,
    pub shell_user: Option<String>,
    /// Legacy; ignored when buddy.config has `[behavior]`.
    #[serde(default = "default_true")]
    pub start_in_status: bool,
    /// Legacy; ignored when buddy.config has `[behavior]`.
    #[serde(default = "default_true")]
    pub keyboard_opens_terminal: bool,
    /// Legacy; ignored when buddy.config has `[behavior]`.
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
            buddy_config: Some(PathBuf::from("/etc/tty-buddy/buddy.config")),
            shell_user: None,
            start_in_status: true,
            keyboard_opens_terminal: true,
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
        // Omit legacy behavior fields; those live in buddy.config.
        #[derive(Serialize)]
        struct HostOnly<'a> {
            device_path: &'a Option<String>,
            vid: Option<u16>,
            pid: Option<u16>,
            serial: &'a Option<String>,
            buddy_config: &'a Option<PathBuf>,
            shell_user: &'a Option<String>,
        }
        let host = HostOnly {
            device_path: &self.device_path,
            vid: self.vid,
            pid: self.pid,
            serial: &self.serial,
            buddy_config: &self.buddy_config,
            shell_user: &self.shell_user,
        };
        let s = toml::to_string_pretty(&host).context("serialize settings")?;
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

/// Configured path, then ./ and /etc buddy.config, then legacy status.config.
pub fn resolve_buddy_config_path(configured: Option<&Path>) -> PathBuf {
    let candidates: Vec<PathBuf> = [
        configured.map(|p| p.to_path_buf()),
        Some(PathBuf::from("buddy.config")),
        Some(PathBuf::from("/etc/tty-buddy/buddy.config")),
        Some(PathBuf::from("status.config")),
        Some(PathBuf::from("/etc/tty-buddy/status.config")),
    ]
    .into_iter()
    .flatten()
    .collect();

    for p in &candidates {
        if p.is_file() {
            return p.clone();
        }
    }
    configured
        .map(|p| p.to_path_buf())
        .unwrap_or_else(|| PathBuf::from("/etc/tty-buddy/buddy.config"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults() {
        let s = DaemonSettings::default();
        assert_eq!(s.vid, Some(0x303A));
        assert_eq!(s.pid, Some(0x1001));
        assert_eq!(
            s.buddy_config.as_deref(),
            Some(std::path::Path::new("/etc/tty-buddy/buddy.config"))
        );
    }

    #[test]
    fn load_missing_uses_defaults() {
        let path = PathBuf::from("/no/such/daemon.toml");
        let s = DaemonSettings::load_or_default(&path);
        assert_eq!(s.vid, Some(0x303A));
    }

    #[test]
    fn round_trip_toml_host_only() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("daemon.toml");
        let s = DaemonSettings {
            device_path: Some("/dev/tty-buddy".into()),
            shell_user: Some("alice".into()),
            start_in_status: false,
            keyboard_opens_terminal: false,
            fps: 12.5,
            ..Default::default()
        };
        s.save(&path).unwrap();
        let text = fs::read_to_string(&path).unwrap();
        assert!(!text.contains("fps"));
        assert!(!text.contains("start_in_status"));
        assert!(!text.contains("keyboard_opens_terminal"));

        let loaded = DaemonSettings::load_or_default(&path);
        assert_eq!(loaded.device_path.as_deref(), Some("/dev/tty-buddy"));
        assert_eq!(loaded.shell_user.as_deref(), Some("alice"));
        assert_eq!(loaded.vid, Some(0x303A));
    }

    #[test]
    fn legacy_status_config_alias() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("daemon.toml");
        fs::write(
            &path,
            "status_config = \"/tmp/old.status.config\"\nshell_user = \"bob\"\n",
        )
        .unwrap();
        let s = DaemonSettings::load_or_default(&path);
        assert_eq!(
            s.buddy_config.as_deref(),
            Some(Path::new("/tmp/old.status.config"))
        );
    }

    #[test]
    fn legacy_behavior_keys_still_parse() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("daemon.toml");
        fs::write(
            &path,
            "device_path = \"/dev/tty-buddy\"\nstart_in_status = false\nkeyboard_opens_terminal = false\nfps = 7.5\n",
        )
        .unwrap();
        let s = DaemonSettings::load_or_default(&path);
        assert!(!s.start_in_status);
        assert!(!s.keyboard_opens_terminal);
        assert_eq!(s.fps, 7.5);
    }

    #[test]
    fn resolve_uses_configured_path() {
        let dir = tempfile::tempdir().unwrap();
        let buddy = dir.path().join("custom.buddy.config");
        fs::write(&buddy, "[behavior]\nfps = 1\n").unwrap();
        assert_eq!(resolve_buddy_config_path(Some(&buddy)), buddy);
    }
}