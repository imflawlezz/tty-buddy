//! Parse buddy.config (INI; legacy name status.config) into UI + behavior.

use std::collections::HashMap;
use std::fs;
use std::path::Path;
use std::time::SystemTime;

use anyhow::Result;

use crate::protocol::{
    parse_hex_color, StatusStyle, AL_CPU, AL_DISK, AL_MEM, AL_SVC_FAILED, AL_SVC_INACTIVE, AL_TEMP,
    METER_OFF, METER_ON, OSD_F_AUTO_BRIGHT, OSD_F_DISMISS_ON_TAP, OSD_F_WAKE_ON_ALERT, SEC_LOAD,
    SEC_NONE, SEC_SWAP, SEC_UPTIME,
};
use crate::settings::DaemonSettings;

#[derive(Debug, Clone)]
pub struct StatusUiConfig {
    pub style: StatusStyle,
    pub date_format: String,
    pub time_format: String,
    pub disk_mount: String,
    pub interfaces: Vec<(String, IpMode)>,
    pub services_filter: Option<Vec<String>>,
    pub mtime: Option<SystemTime>,
    pub startup_status: bool,
    pub keyboard_opens_terminal: bool,
    pub fps: f32,
    pub keyboard_layout: crate::keyboard::KeyboardLayout,
    /// Empty allowlist opens no keyboards.
    pub keyboard_devices: Vec<String>,
    /// Set when `[behavior]` is present; otherwise daemon.toml legacy keys apply.
    pub behavior_from_file: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum IpMode {
    V4,
    V6,
    /// Prefer IPv4, else global IPv6, else link-local.
    Auto,
}

impl Default for StatusUiConfig {
    fn default() -> Self {
        Self {
            style: StatusStyle::default(),
            date_format: "%d-%m-%Y".into(),
            time_format: "%H:%M:%S".into(),
            disk_mount: "/".into(),
            interfaces: Vec::new(),
            services_filter: None,
            mtime: None,
            startup_status: true,
            keyboard_opens_terminal: false,
            fps: 10.0,
            keyboard_layout: crate::keyboard::KeyboardLayout::Us,
            keyboard_devices: Vec::new(),
            behavior_from_file: false,
        }
    }
}

fn strip_inline_comment(s: &str) -> &str {
    let bytes = s.as_bytes();
    for i in 0..bytes.len() {
        if (bytes[i] == b'#' || bytes[i] == b';') && i > 0 && bytes[i - 1].is_ascii_whitespace() {
            return s[..i].trim_end();
        }
    }
    s
}

type IniSections = HashMap<String, HashMap<String, String>>;
type IniParse = (IniSections, Vec<(String, String)>);

fn parse_ini(text: &str) -> IniParse {
    let mut sections: IniSections = HashMap::new();
    let mut iface_order: Vec<(String, String)> = Vec::new();
    let mut current = String::new();
    for raw in text.lines() {
        let line = raw.trim();
        if line.is_empty() || line.starts_with('#') || line.starts_with(';') {
            continue;
        }
        if line.starts_with('[') && line.ends_with(']') {
            current = line[1..line.len() - 1].trim().to_ascii_lowercase();
            sections.entry(current.clone()).or_default();
            continue;
        }
        if current.is_empty() {
            continue;
        }
        if let Some((k, v)) = line.split_once('=') {
            let key = k.trim().to_ascii_lowercase();
            let val = strip_inline_comment(v.trim()).to_string();
            if current == "interfaces" {
                iface_order.push((key.clone(), val.clone()));
            }
            sections
                .entry(current.clone())
                .or_default()
                .insert(key, val);
        }
    }
    (sections, iface_order)
}

fn as_bool(s: &str, default: bool) -> bool {
    match s.trim().to_ascii_lowercase().as_str() {
        "1" | "true" | "yes" | "on" => true,
        "0" | "false" | "no" | "off" => false,
        "" => default,
        _ => default,
    }
}

fn sec_id(s: &str) -> u8 {
    match s.trim().to_ascii_lowercase().as_str() {
        "uptime" | "up" => SEC_UPTIME,
        "swap" => SEC_SWAP,
        "load" => SEC_LOAD,
        "" | "none" | "blank" => SEC_NONE,
        _ => SEC_NONE,
    }
}

pub fn load_status_config(path: &Path) -> Result<StatusUiConfig> {
    if !path.is_file() {
        return Ok(StatusUiConfig::default());
    }
    let text = fs::read_to_string(path)?;
    let mtime = fs::metadata(path).ok().and_then(|m| m.modified().ok());
    let mut cfg = parse_status_config_from_str(&text);
    cfg.mtime = mtime;
    Ok(cfg)
}

pub(crate) fn parse_status_config_from_str(text: &str) -> StatusUiConfig {
    let (sec, iface_order) = parse_ini(text);
    let mut cfg = StatusUiConfig::default();
    let mut st = StatusStyle::default();

    if let Some(b) = sec.get("behavior") {
        cfg.behavior_from_file = true;
        if let Some(v) = b.get("startup_mode").or_else(|| b.get("start_in_status")) {
            let t = v.trim().to_ascii_lowercase();
            cfg.startup_status = !matches!(
                t.as_str(),
                "terminal" | "console" | "tty" | "false" | "0" | "no" | "off"
            );
        }
        if let Some(v) = b.get("keyboard_opens_terminal") {
            cfg.keyboard_opens_terminal = as_bool(v, false);
        }
        if let Some(v) = b.get("fps") {
            if let Ok(n) = v.parse::<f32>() {
                cfg.fps = n.max(1.0);
            }
        }
        if let Some(v) = b.get("keyboard_layout") {
            if let Some(layout) = crate::keyboard::KeyboardLayout::parse(v) {
                cfg.keyboard_layout = layout;
            }
        }
        if let Some(v) = b
            .get("keyboard_devices")
            .or_else(|| b.get("keyboard_device"))
        {
            cfg.keyboard_devices = crate::keyboard::parse_keyboard_devices(v);
        }
    }

    if let Some(g) = sec.get("globals") {
        if let Some(v) = g.get("label_color") {
            st.label_c = parse_hex_color(v, st.label_c);
        }
        if let Some(v) = g.get("background_color").or_else(|| g.get("background")) {
            st.bg_c = parse_hex_color(v, st.bg_c);
        }
    }
    if let Some(h) = sec.get("header") {
        if let Some(v) = h.get("hostname_color") {
            st.host_c = parse_hex_color(v, 0xFFFF);
        }
        if let Some(v) = h.get("date_color") {
            st.date_c = parse_hex_color(v, st.date_c);
        }
        if let Some(v) = h.get("time_color") {
            st.time_c = parse_hex_color(v, 0xFFFF);
        }
        if let Some(v) = h.get("date_format") {
            if !v.is_empty() {
                cfg.date_format = v.clone();
            }
        }
        if let Some(v) = h.get("time_format") {
            if !v.is_empty() {
                cfg.time_format = v.clone();
            }
        }
    }
    if let Some(hero) = sec.get("hero") {
        st.meter_mode = if as_bool(
            hero.get("meter_mode").map(|s| s.as_str()).unwrap_or("true"),
            true,
        ) {
            METER_ON
        } else {
            METER_OFF
        };
        st.hero_cpu_c = parse_hex_color(
            hero.get("cpu_color")
                .or_else(|| hero.get("cpu"))
                .map(|s| s.as_str())
                .unwrap_or(""),
            0xFFFF,
        );
        st.hero_mem_c = parse_hex_color(
            hero.get("mem_color")
                .or_else(|| hero.get("mem"))
                .map(|s| s.as_str())
                .unwrap_or(""),
            0xFFFF,
        );
        st.hero_disk_c = parse_hex_color(
            hero.get("disk_color")
                .or_else(|| hero.get("disk"))
                .map(|s| s.as_str())
                .unwrap_or(""),
            0xFFFF,
        );
        st.level_ok = parse_hex_color(
            hero.get("level_ok_color")
                .or_else(|| hero.get("level_ok"))
                .map(|s| s.as_str())
                .unwrap_or(""),
            st.level_ok,
        );
        st.level_warn = parse_hex_color(
            hero.get("level_warn_color")
                .or_else(|| hero.get("level_warn"))
                .map(|s| s.as_str())
                .unwrap_or(""),
            st.level_warn,
        );
        st.level_crit = parse_hex_color(
            hero.get("level_crit_color")
                .or_else(|| hero.get("level_crit"))
                .map(|s| s.as_str())
                .unwrap_or(""),
            st.level_crit,
        );
        if let Some(v) = hero.get("warn_at") {
            st.warn_at = v.parse().unwrap_or(60);
        }
        if let Some(v) = hero.get("crit_at") {
            st.crit_at = v.parse().unwrap_or(90);
        }
        if st.warn_at > st.crit_at {
            std::mem::swap(&mut st.warn_at, &mut st.crit_at);
        }
        if let Some(v) = hero.get("disk_mount") {
            if !v.is_empty() {
                cfg.disk_mount = v.clone();
            }
        }
    }
    if let Some(secondary) = sec.get("secondary") {
        st.sec_left = sec_id(secondary.get("left").map(|s| s.as_str()).unwrap_or(""));
        st.sec_right = sec_id(secondary.get("right").map(|s| s.as_str()).unwrap_or(""));
        st.sec_left_c = parse_hex_color(
            secondary
                .get("left_color")
                .map(|s| s.as_str())
                .unwrap_or(""),
            0xFFFF,
        );
        st.sec_right_c = parse_hex_color(
            secondary
                .get("right_color")
                .map(|s| s.as_str())
                .unwrap_or(""),
            0xFFFF,
        );
    }
    if !iface_order.is_empty() {
        for (name, mode) in iface_order {
            let m = match mode.trim().to_ascii_lowercase().as_str() {
                "v6" | "6" | "ipv6" => IpMode::V6,
                "auto" | "any" | "both" => IpMode::Auto,
                _ => IpMode::V4,
            };
            cfg.interfaces.push((name, m));
        }
    } else if let Some(ifaces) = sec.get("interfaces") {
        for (name, mode) in ifaces {
            let m = match mode.trim().to_ascii_lowercase().as_str() {
                "v6" | "6" | "ipv6" => IpMode::V6,
                "auto" | "any" | "both" => IpMode::Auto,
                _ => IpMode::V4,
            };
            cfg.interfaces.push((name.clone(), m));
        }
    }
    if let Some(services) = sec.get("services") {
        let filter = services.get("filter").map(|s| s.as_str()).unwrap_or("all");
        let f = filter.trim();
        if f.is_empty() || f.eq_ignore_ascii_case("all") || f == "*" {
            cfg.services_filter = None;
        } else {
            cfg.services_filter = Some(
                f.split(',')
                    .map(|s| s.trim().to_string())
                    .filter(|s| !s.is_empty())
                    .collect(),
            );
        }
        let def = StatusStyle::default();
        let svc_color = |key_new: &str, key_old: &str, default: u16| {
            parse_hex_color(
                services
                    .get(key_new)
                    .or_else(|| services.get(key_old))
                    .map(|s| s.as_str())
                    .unwrap_or(""),
                default,
            )
        };
        st.svc_active = svc_color("active_color", "active", def.svc_active);
        st.svc_failed = svc_color("failed_color", "failed", def.svc_failed);
        st.svc_inactive = svc_color("inactive_color", "inactive", def.svc_inactive);
        st.svc_activating = svc_color("activating_color", "activating", def.svc_activating);
        st.svc_reloading = svc_color("reloading_color", "reloading", def.svc_reloading);
        st.svc_deactivating = svc_color("deactivating_color", "deactivating", def.svc_deactivating);
        st.svc_maintenance = svc_color("maintenance_color", "maintenance", def.svc_maintenance);
    }

    if let Some(alerts) = sec.get("alerts") {
        st.alert_bg_c = parse_hex_color(
            alerts
                .get("background_color")
                .map(|s| s.as_str())
                .unwrap_or(""),
            st.alert_bg_c,
        );
        st.alert_fg_c = parse_hex_color(
            alerts.get("text_color").map(|s| s.as_str()).unwrap_or(""),
            st.alert_fg_c,
        );
        if let Some(v) = alerts.get("hold_sec") {
            let t = v.trim().to_ascii_lowercase();
            st.alert_hold_sec = if t.is_empty()
                || t == "0"
                || t == "until_clear"
                || t == "until-clear"
                || t == "clear"
            {
                0
            } else {
                v.parse().unwrap_or(0)
            };
        }
        if let Some(v) = alerts.get("temp_crit_c") {
            st.alert_temp_c = v.parse().unwrap_or(80);
        }
        let mut mask = 0u8;
        if as_bool(
            alerts.get("cpu_crit").map(|s| s.as_str()).unwrap_or("true"),
            true,
        ) {
            mask |= AL_CPU;
        }
        if as_bool(
            alerts.get("mem_crit").map(|s| s.as_str()).unwrap_or("true"),
            true,
        ) {
            mask |= AL_MEM;
        }
        if as_bool(
            alerts
                .get("disk_crit")
                .map(|s| s.as_str())
                .unwrap_or("true"),
            true,
        ) {
            mask |= AL_DISK;
        }
        if as_bool(
            alerts
                .get("temp_crit")
                .map(|s| s.as_str())
                .unwrap_or("true"),
            true,
        ) {
            mask |= AL_TEMP;
        }
        if as_bool(
            alerts
                .get("service_failed")
                .map(|s| s.as_str())
                .unwrap_or("true"),
            true,
        ) {
            mask |= AL_SVC_FAILED;
        }
        if as_bool(
            alerts
                .get("service_inactive")
                .map(|s| s.as_str())
                .unwrap_or("false"),
            false,
        ) {
            mask |= AL_SVC_INACTIVE;
        }
        if let Some(v) = alerts.get("enabled") {
            if !as_bool(v, true) {
                mask = 0;
            }
        }
        st.alert_mask = mask;
    }

    let display = sec.get("display").or_else(|| sec.get("osd"));
    if let Some(osd) = display {
        let bright_auto = osd
            .get("brightness")
            .or_else(|| osd.get("default_brightness"))
            .map(|s| s.trim().eq_ignore_ascii_case("auto"))
            .unwrap_or(false);
        if let Some(v) = osd
            .get("brightness")
            .or_else(|| osd.get("default_brightness"))
        {
            if bright_auto {
                st.osd_default_bright_pct = 0;
            } else if let Ok(n) = v.parse::<u8>() {
                st.osd_default_bright_pct = n.min(100);
            }
        }
        if let Some(v) = osd
            .get("sleep_timeout")
            .or_else(|| osd.get("sleep_timeout_sec"))
        {
            let t = v.trim().to_ascii_lowercase();
            st.osd_sleep_timeout_s = if t.is_empty() || t == "never" {
                0
            } else {
                v.parse().unwrap_or(st.osd_sleep_timeout_s)
            };
        }
        let mut flags = 0u8;
        if as_bool(
            osd.get("dismiss_alert_on_tap")
                .map(|s| s.as_str())
                .unwrap_or("true"),
            true,
        ) {
            flags |= OSD_F_DISMISS_ON_TAP;
        }
        if bright_auto
            || as_bool(
                osd.get("auto_brightness")
                    .map(|s| s.as_str())
                    .unwrap_or("false"),
                false,
            )
        {
            flags |= OSD_F_AUTO_BRIGHT;
            if bright_auto {
                st.osd_default_bright_pct = 0;
            }
        }
        if as_bool(
            osd.get("wake_on_alert")
                .map(|s| s.as_str())
                .unwrap_or("true"),
            true,
        ) {
            flags |= OSD_F_WAKE_ON_ALERT;
        }
        st.osd_flags = flags;
        if let Some(v) = osd
            .get("auto_day_level")
            .or_else(|| osd.get("auto_day_pct"))
        {
            st.osd_auto_day_pct = v.parse().unwrap_or(st.osd_auto_day_pct).min(100);
        }
        if let Some(v) = osd
            .get("auto_night_level")
            .or_else(|| osd.get("auto_night_pct"))
        {
            st.osd_auto_night_pct = v.parse().unwrap_or(st.osd_auto_night_pct).min(100);
        }
        if let Some(v) = osd.get("auto_day_hour") {
            st.osd_auto_day_hour = v.parse().unwrap_or(st.osd_auto_day_hour).min(23);
        }
        if let Some(v) = osd.get("auto_night_hour") {
            st.osd_auto_night_hour = v.parse().unwrap_or(st.osd_auto_night_hour).min(23);
        }
    }

    cfg.style = st;
    cfg
}

/// Copy daemon.toml behavior keys when buddy.config has no `[behavior]`.
pub fn apply_daemon_behavior_fallback(cfg: &mut StatusUiConfig, settings: &DaemonSettings) {
    if cfg.behavior_from_file {
        return;
    }
    cfg.startup_status = settings.start_in_status;
    cfg.keyboard_opens_terminal = settings.keyboard_opens_terminal;
    cfg.fps = settings.fps.max(1.0);
}

pub fn maybe_reload(path: &Path, current: &StatusUiConfig) -> Option<StatusUiConfig> {
    let mtime = fs::metadata(path).ok().and_then(|m| m.modified().ok())?;
    if current.mtime == Some(mtime) {
        return None;
    }
    load_status_config(path).ok()
}

/// Write brightness/sleep into `[display]` (0 = auto/never). Renames legacy `[osd]`.
pub fn write_osd_levels(path: &Path, bright: u8, sleep: u8) -> Result<()> {
    let bright = bright.min(6);
    let sleep = sleep.min(6);
    let bright_val = if bright == 0 {
        "auto".to_string()
    } else {
        bright.to_string()
    };
    let sleep_val = if sleep == 0 {
        "never".to_string()
    } else {
        sleep.to_string()
    };

    let original = if path.is_file() {
        fs::read_to_string(path)?
    } else {
        String::new()
    };

    let mut out = String::with_capacity(original.len() + 64);
    let mut in_display = false;
    let mut saw_display = false;
    let mut wrote_bright = false;
    let mut wrote_sleep = false;

    for line in original.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with('[') && trimmed.ends_with(']') {
            if in_display {
                if !wrote_bright {
                    out.push_str(&format!("brightness = {bright_val}\n"));
                    wrote_bright = true;
                }
                if !wrote_sleep {
                    out.push_str(&format!("sleep_timeout = {sleep_val}\n"));
                    wrote_sleep = true;
                }
            }
            let is_display =
                trimmed.eq_ignore_ascii_case("[display]") || trimmed.eq_ignore_ascii_case("[osd]");
            in_display = is_display;
            if in_display {
                saw_display = true;
                out.push_str("[display]\n");
            } else {
                out.push_str(line);
                out.push('\n');
            }
            continue;
        }
        if in_display {
            let key = trimmed.split('=').next().map(str::trim).unwrap_or("");
            if key.eq_ignore_ascii_case("brightness")
                || key.eq_ignore_ascii_case("default_brightness")
            {
                if !wrote_bright {
                    out.push_str(&format!("brightness = {bright_val}\n"));
                    wrote_bright = true;
                }
                continue;
            }
            if key.eq_ignore_ascii_case("sleep_timeout")
                || key.eq_ignore_ascii_case("sleep_timeout_sec")
            {
                if !wrote_sleep {
                    out.push_str(&format!("sleep_timeout = {sleep_val}\n"));
                    wrote_sleep = true;
                }
                continue;
            }
            // Drop legacy auto_brightness — brightness=auto is enough.
            if key.eq_ignore_ascii_case("auto_brightness") {
                continue;
            }
        }
        out.push_str(line);
        out.push('\n');
    }
    if in_display {
        if !wrote_bright {
            out.push_str(&format!("brightness = {bright_val}\n"));
        }
        if !wrote_sleep {
            out.push_str(&format!("sleep_timeout = {sleep_val}\n"));
        }
    } else if !saw_display {
        if !out.is_empty() && !out.ends_with('\n') {
            out.push('\n');
        }
        out.push_str(&format!(
            "\n[display]\nbrightness = {bright_val}\nsleep_timeout = {sleep_val}\n"
        ));
    }

    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    atomic_write(path, &out)?;
    Ok(())
}

/// Temp+rename when the parent dir is writable; otherwise overwrite in place
/// (e.g. root-owned `/etc/tty-buddy`, user-owned `buddy.config`).
fn atomic_write(path: &Path, contents: &str) -> Result<()> {
    let parent = path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    let tmp = parent.join(format!(
        ".{}.{}.tmp",
        path.file_name()
            .and_then(|s| s.to_str())
            .unwrap_or("buddy.config"),
        std::process::id()
    ));
    match fs::write(&tmp, contents) {
        Ok(()) => {
            if let Err(e) = fs::rename(&tmp, path) {
                let _ = fs::remove_file(&tmp);
                return Err(e.into());
            }
            Ok(())
        }
        Err(e) if e.kind() == std::io::ErrorKind::PermissionDenied => {
            fs::write(path, contents)?;
            Ok(())
        }
        Err(e) => Err(e.into()),
    }
}

pub fn apply_osd_levels(cfg: &mut StatusUiConfig, bright: u8, sleep: u8) {
    let bright = bright.min(6);
    let sleep = sleep.min(6);
    cfg.style.osd_default_bright_pct = bright;
    cfg.style.osd_sleep_timeout_s = u16::from(sleep);
    if bright == 0 {
        cfg.style.osd_flags |= OSD_F_AUTO_BRIGHT;
    } else {
        cfg.style.osd_flags &= !OSD_F_AUTO_BRIGHT;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::protocol::{
        rgb565, AL_CPU, AL_DISK, AL_MEM, AL_SVC_FAILED, AL_SVC_INACTIVE, AL_TEMP, METER_OFF,
        METER_ON, OSD_F_AUTO_BRIGHT, OSD_F_DISMISS_ON_TAP, OSD_F_WAKE_ON_ALERT, SEC_LOAD, SEC_SWAP,
        SEC_UPTIME,
    };
    use std::io::Write;
    use std::thread;
    use std::time::Duration;

    #[test]
    fn missing_file_returns_defaults() {
        let cfg = load_status_config(Path::new("/no/such/buddy.config")).unwrap();
        assert_eq!(cfg.date_format, "%d-%m-%Y");
        assert_eq!(cfg.disk_mount, "/");
        assert!(cfg.interfaces.is_empty());
        assert!(cfg.services_filter.is_none());
        assert_eq!(cfg.style.meter_mode, METER_ON);
        assert!(cfg.startup_status);
        assert!(!cfg.keyboard_opens_terminal);
        assert_eq!(cfg.fps, 10.0);
        assert!(!cfg.behavior_from_file);
    }

    #[test]
    fn parses_new_color_keys_and_formats() {
        let text = r#"
[globals]
label_color = #112233
background_color = #000000

[header]
hostname_color = #FFFFFF
date_format = %Y-%m-%d
time_format = %H:%M

[hero]
cpu_color = #FF0000
mem_color = #00FF00
disk_color = #0000FF
level_ok_color = #111111
level_warn_color = #222222
level_crit_color = #333333
meter_mode = false
warn_at = 55
crit_at = 88
disk_mount = /data

[secondary]
left = uptime
right = load
left_color = #AAAAAA
right_color = #BBBBBB

[interfaces]
eth0 = v4
wlan0 = auto
br0 = ipv6

[services]
filter = ssh, docker.service
active_color = #010101
failed_color = #020202
"#;
        let cfg = parse_status_config_from_str(text);
        assert_eq!(cfg.style.label_c, parse_hex_color("#112233", 0));
        assert_eq!(cfg.style.bg_c, 0);
        assert_eq!(cfg.date_format, "%Y-%m-%d");
        assert_eq!(cfg.time_format, "%H:%M");
        assert_eq!(cfg.disk_mount, "/data");
        assert_eq!(cfg.style.meter_mode, METER_OFF);
        assert_eq!(cfg.style.warn_at, 55);
        assert_eq!(cfg.style.crit_at, 88);
        assert_eq!(cfg.style.hero_cpu_c, rgb565(0xFF, 0x00, 0x00));
        assert_eq!(cfg.style.hero_mem_c, rgb565(0x00, 0xFF, 0x00));
        assert_eq!(cfg.style.hero_disk_c, rgb565(0x00, 0x00, 0xFF));
        assert_eq!(cfg.style.sec_left, SEC_UPTIME);
        assert_eq!(cfg.style.sec_right, SEC_LOAD);
        assert_eq!(
            cfg.interfaces,
            vec![
                ("eth0".into(), IpMode::V4),
                ("wlan0".into(), IpMode::Auto),
                ("br0".into(), IpMode::V6),
            ]
        );
        assert_eq!(
            cfg.services_filter,
            Some(vec!["ssh".into(), "docker.service".into()])
        );
        assert_eq!(cfg.style.svc_active, parse_hex_color("#010101", 0));
        assert_eq!(cfg.style.svc_failed, parse_hex_color("#020202", 0));
    }

    #[test]
    fn accepts_legacy_color_keys_and_all_filter() {
        let text = r#"
[globals]
background = #010203
[hero]
cpu = #ABCDEF
level_ok = #111111
[secondary]
left = swap
right = none
[services]
filter = all
active = #445566
"#;
        let cfg = parse_status_config_from_str(text);
        assert_eq!(cfg.style.bg_c, parse_hex_color("#010203", 0));
        assert_eq!(cfg.style.hero_cpu_c, parse_hex_color("#ABCDEF", 0));
        assert_eq!(cfg.style.level_ok, parse_hex_color("#111111", 0));
        assert_eq!(cfg.style.sec_left, SEC_SWAP);
        assert_eq!(cfg.style.sec_right, SEC_NONE);
        assert!(cfg.services_filter.is_none());
        assert_eq!(cfg.style.svc_active, parse_hex_color("#445566", 0));
    }

    #[test]
    fn parses_alerts_section() {
        let text = r#"
[alerts]
enabled = true
background_color = #AA0000
text_color = #EEEEEE
hold_sec = 12
temp_crit_c = 75
cpu_crit = true
mem_crit = false
disk_crit = true
temp_crit = true
service_failed = true
service_inactive = true
"#;
        let cfg = parse_status_config_from_str(text);
        assert_eq!(cfg.style.alert_bg_c, parse_hex_color("#AA0000", 0));
        assert_eq!(cfg.style.alert_fg_c, parse_hex_color("#EEEEEE", 0));
        assert_eq!(cfg.style.alert_hold_sec, 12);
        assert_eq!(cfg.style.alert_temp_c, 75);
        assert_eq!(
            cfg.style.alert_mask,
            AL_CPU | AL_DISK | AL_TEMP | AL_SVC_FAILED | AL_SVC_INACTIVE
        );
        assert_eq!(cfg.style.alert_mask & AL_MEM, 0);
    }

    #[test]
    fn alerts_disabled_clears_mask() {
        let text = "[alerts]\nenabled = false\ncpu_crit = true\n";
        let cfg = parse_status_config_from_str(text);
        assert_eq!(cfg.style.alert_mask, 0);
    }

    #[test]
    fn parses_display_section() {
        let text = r#"
[display]
brightness = 4
sleep_timeout = 3
dismiss_alert_on_tap = false
auto_brightness = true
auto_day_level = 5
auto_night_level = 2
auto_day_hour = 6
auto_night_hour = 22
"#;
        let cfg = parse_status_config_from_str(text);
        assert_eq!(cfg.style.osd_default_bright_pct, 4);
        assert_eq!(cfg.style.osd_sleep_timeout_s, 3);
        assert_eq!(cfg.style.osd_flags, OSD_F_AUTO_BRIGHT | OSD_F_WAKE_ON_ALERT);
        assert_eq!(cfg.style.osd_auto_day_pct, 5);
        assert_eq!(cfg.style.osd_auto_night_pct, 2);
        assert_eq!(cfg.style.osd_auto_day_hour, 6);
        assert_eq!(cfg.style.osd_auto_night_hour, 22);
    }

    #[test]
    fn parses_legacy_osd_section_name() {
        let text = r#"
[osd]
default_brightness = 4
sleep_timeout = 3
dismiss_alert_on_tap = false
auto_brightness = true
auto_day_level = 5
auto_night_level = 2
auto_day_hour = 6
auto_night_hour = 22
"#;
        let cfg = parse_status_config_from_str(text);
        assert_eq!(cfg.style.osd_default_bright_pct, 4);
        assert_eq!(cfg.style.osd_sleep_timeout_s, 3);
        assert_eq!(cfg.style.osd_flags, OSD_F_AUTO_BRIGHT | OSD_F_WAKE_ON_ALERT);
    }

    #[test]
    fn osd_brightness_auto_keyword() {
        let cfg = parse_status_config_from_str("[display]\nbrightness = auto\n");
        assert_eq!(cfg.style.osd_default_bright_pct, 0);
        assert_eq!(cfg.style.osd_flags & OSD_F_AUTO_BRIGHT, OSD_F_AUTO_BRIGHT);
        assert_eq!(
            cfg.style.osd_flags & OSD_F_WAKE_ON_ALERT,
            OSD_F_WAKE_ON_ALERT
        );
    }

    #[test]
    fn osd_section_defaults_when_absent() {
        let cfg = parse_status_config_from_str("");
        let def = StatusStyle::default();
        assert_eq!(cfg.style.osd_default_bright_pct, def.osd_default_bright_pct);
        assert_eq!(cfg.style.osd_sleep_timeout_s, def.osd_sleep_timeout_s);
        assert_eq!(cfg.style.osd_flags, def.osd_flags);
    }

    #[test]
    fn osd_sleep_timeout_never_is_zero() {
        let cfg = parse_status_config_from_str("[display]\nsleep_timeout = never\n");
        assert_eq!(cfg.style.osd_sleep_timeout_s, 0);
    }

    #[test]
    fn osd_wake_on_alert_can_disable() {
        let cfg = parse_status_config_from_str("[display]\nwake_on_alert = false\n");
        assert_eq!(cfg.style.osd_flags & OSD_F_WAKE_ON_ALERT, 0);
        assert_eq!(
            cfg.style.osd_flags & OSD_F_DISMISS_ON_TAP,
            OSD_F_DISMISS_ON_TAP
        );
    }

    #[test]
    fn strips_inline_comments_and_blank_lines() {
        let text = r#"
# top comment
[header]
date_format = %d-%m-%Y ; european
time_format = %H:%M:%S # 24h
"#;
        let cfg = parse_status_config_from_str(text);
        assert_eq!(cfg.date_format, "%d-%m-%Y");
        assert_eq!(cfg.time_format, "%H:%M:%S");
    }

    #[test]
    fn maybe_reload_detects_mtime_change() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("status.config");
        {
            let mut f = fs::File::create(&path).unwrap();
            writeln!(f, "[header]\ndate_format = %Y").unwrap();
        }
        let cfg = load_status_config(&path).unwrap();
        assert_eq!(cfg.date_format, "%Y");
        assert!(maybe_reload(&path, &cfg).is_none());

        thread::sleep(Duration::from_millis(20));
        fs::write(&path, "[header]\ndate_format = %m\n").unwrap();
        let reloaded = maybe_reload(&path, &cfg).expect("mtime change");
        assert_eq!(reloaded.date_format, "%m");
    }

    #[test]
    fn write_osd_levels_updates_section() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("status.config");
        fs::write(
            &path,
            "[osd]\nbrightness = 4\nsleep_timeout = 4\nwake_on_alert = true\n",
        )
        .unwrap();
        write_osd_levels(&path, 0, 2).unwrap();
        let text = fs::read_to_string(&path).unwrap();
        assert!(text.contains("[display]"));
        assert!(!text.contains("[osd]"));
        assert!(text.contains("brightness = auto"));
        assert!(text.contains("sleep_timeout = 2"));
        assert!(!text.contains("default_brightness"));
        assert!(!text.contains("auto_brightness"));
        assert!(text.contains("wake_on_alert = true"));
        let cfg = load_status_config(&path).unwrap();
        assert_eq!(cfg.style.osd_default_bright_pct, 0);
        assert_eq!(cfg.style.osd_sleep_timeout_s, 2);
        assert_eq!(cfg.style.osd_flags & OSD_F_AUTO_BRIGHT, OSD_F_AUTO_BRIGHT);
        let tmps: Vec<_> = fs::read_dir(dir.path())
            .unwrap()
            .filter_map(|e| e.ok())
            .filter(|e| e.file_name().to_string_lossy().contains(".tmp"))
            .collect();
        assert!(tmps.is_empty(), "atomic write left temp files: {tmps:?}");
    }

    #[test]
    fn display_preferred_over_legacy_osd() {
        let text = r#"
[osd]
brightness = 1
[display]
brightness = 5
"#;
        let cfg = parse_status_config_from_str(text);
        assert_eq!(cfg.style.osd_default_bright_pct, 5);
    }

    #[test]
    fn parses_behavior_section() {
        let text = r#"
[behavior]
startup_mode = terminal
keyboard_opens_terminal = false
fps = 12
keyboard_layout = de
keyboard_devices = /dev/input/by-id/usb-kbd-event-kbd, Logitech
"#;
        let cfg = parse_status_config_from_str(text);
        assert!(cfg.behavior_from_file);
        assert!(!cfg.startup_status);
        assert!(!cfg.keyboard_opens_terminal);
        assert_eq!(cfg.fps, 12.0);
        assert_eq!(cfg.keyboard_layout, crate::keyboard::KeyboardLayout::De);
        assert_eq!(
            cfg.keyboard_devices,
            vec![
                "/dev/input/by-id/usb-kbd-event-kbd".to_string(),
                "Logitech".to_string()
            ]
        );
    }

    #[test]
    fn daemon_fallback_when_no_behavior() {
        let mut cfg = parse_status_config_from_str("[header]\ndate_format = %Y\n");
        assert!(!cfg.behavior_from_file);
        let settings = DaemonSettings {
            start_in_status: false,
            keyboard_opens_terminal: false,
            fps: 8.0,
            ..Default::default()
        };
        apply_daemon_behavior_fallback(&mut cfg, &settings);
        assert!(!cfg.startup_status);
        assert!(!cfg.keyboard_opens_terminal);
        assert_eq!(cfg.fps, 8.0);
    }

    #[test]
    fn normalizes_warn_and_crit_order() {
        let cfg = parse_status_config_from_str("[hero]\nwarn_at = 95\ncrit_at = 60\n");
        assert_eq!(cfg.style.warn_at, 60);
        assert_eq!(cfg.style.crit_at, 95);
    }
}
