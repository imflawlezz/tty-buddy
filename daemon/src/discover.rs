//! USB serial discovery (Linux sysfs + optional saved settings).

use std::fmt;
use std::fs;
use std::path::{Path, PathBuf};

use anyhow::Result;

use crate::settings::DaemonSettings;

pub const ESP_VID: u16 = 0x303A;
pub const ESP_PID_JTAG: u16 = 0x1001;

#[derive(Debug, Clone)]
pub struct SerialDevice {
    pub path: String,
    pub vid: Option<u16>,
    pub pid: Option<u16>,
    pub serial: Option<String>,
    pub product: Option<String>,
    pub manufacturer: Option<String>,
}

impl fmt::Display for SerialDevice {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.path)?;
        match (self.vid, self.pid) {
            (Some(v), Some(p)) => write!(f, "  {:04x}:{:04x}", v, p)?,
            _ => write!(f, "  ????")?,
        }
        if let Some(ref m) = self.manufacturer {
            write!(f, "  {m}")?;
        }
        if let Some(ref p) = self.product {
            write!(f, "  {p}")?;
        }
        if let Some(ref s) = self.serial {
            write!(f, "  serial={s}")?;
        }
        if self.vid == Some(ESP_VID) {
            write!(f, "  [espressif]")?;
        }
        Ok(())
    }
}

fn read_hex_u16(path: &Path) -> Option<u16> {
    let s = fs::read_to_string(path).ok()?;
    u16::from_str_radix(s.trim(), 16).ok()
}

fn read_trim(path: &Path) -> Option<String> {
    let s = fs::read_to_string(path).ok()?;
    let t = s.trim().to_string();
    if t.is_empty() {
        None
    } else {
        Some(t)
    }
}

fn usb_props_for_tty(tty_name: &str) -> (Option<u16>, Option<u16>, Option<String>, Option<String>, Option<String>) {
    // Walk sysfs parents until idVendor is found (USB device node).
    let mut cur = PathBuf::from(format!("/sys/class/tty/{tty_name}/device"));
    for _ in 0..8 {
        let vid_p = cur.join("idVendor");
        if vid_p.is_file() {
            return (
                read_hex_u16(&vid_p),
                read_hex_u16(&cur.join("idProduct")),
                read_trim(&cur.join("serial")),
                read_trim(&cur.join("product")),
                read_trim(&cur.join("manufacturer")),
            );
        }
        let parent = cur.join("..");
        let Ok(canon) = fs::canonicalize(&parent) else {
            break;
        };
        cur = canon;
    }
    (None, None, None, None, None)
}

pub fn list_serial_devices() -> Result<Vec<SerialDevice>> {
    let mut out = Vec::new();
    let mut seen = std::collections::HashSet::new();

    // List /dev/tty-buddy first when present (stable udev symlink).
    for stable in ["/dev/tty-buddy"] {
        if Path::new(stable).exists() {
            let real = fs::canonicalize(stable)
                .map(|p| p.to_string_lossy().to_string())
                .unwrap_or_else(|_| stable.to_string());
            if seen.insert(real.clone()) {
                let base = Path::new(&real)
                    .file_name()
                    .and_then(|s| s.to_str())
                    .unwrap_or("");
                let (vid, pid, serial, product, manufacturer) = usb_props_for_tty(base);
                out.push(SerialDevice {
                    path: stable.to_string(),
                    vid,
                    pid,
                    serial,
                    product,
                    manufacturer,
                });
            }
        }
    }

    let mut nodes: Vec<PathBuf> = Vec::new();
    for pat in ["/dev/ttyACM*", "/dev/ttyUSB*"] {
        if let Ok(paths) = glob_dev(pat) {
            nodes.extend(paths);
        }
    }
    nodes.sort();
    for path in nodes {
        let s = path.to_string_lossy().to_string();
        let real = fs::canonicalize(&path)
            .map(|p| p.to_string_lossy().to_string())
            .unwrap_or_else(|_| s.clone());
        if !seen.insert(real.clone()) {
            continue;
        }
        let base = path
            .file_name()
            .and_then(|x| x.to_str())
            .unwrap_or("");
        let (vid, pid, serial, product, manufacturer) = usb_props_for_tty(base);
        out.push(SerialDevice {
            path: s,
            vid,
            pid,
            serial,
            product,
            manufacturer,
        });
    }

    out.sort_by_key(|d| {
        (
            if d.vid == Some(ESP_VID) { 0 } else { 1 },
            d.path.clone(),
        )
    });
    Ok(out)
}

fn glob_dev(pattern: &str) -> Result<Vec<PathBuf>> {
    let dir = Path::new("/dev");
    let prefix = pattern.rsplit('/').next().unwrap_or("");
    let (pre, _) = prefix.split_once('*').unwrap_or((prefix, ""));
    let mut v = Vec::new();
    if let Ok(rd) = fs::read_dir(dir) {
        for e in rd.flatten() {
            let name = e.file_name();
            let n = name.to_string_lossy();
            if n.starts_with(pre) {
                v.push(e.path());
            }
        }
    }
    Ok(v)
}

pub fn resolve_device(settings: &DaemonSettings) -> Result<String> {
    let devices = list_serial_devices()?;

    if let Some(ref path) = settings.device_path {
        if Path::new(path).exists() {
            return Ok(path.clone());
        }
    }

    if let (Some(vid), Some(pid)) = (settings.vid, settings.pid) {
        let matches: Vec<_> = devices
            .iter()
            .filter(|d| d.vid == Some(vid) && d.pid == Some(pid))
            .collect();
        if let Some(ref ser) = settings.serial {
            if let Some(d) = matches.iter().find(|d| d.serial.as_deref() == Some(ser.as_str())) {
                return Ok(d.path.clone());
            }
        }
        if matches.len() == 1 {
            return Ok(matches[0].path.clone());
        }
        if let Some(d) = matches.first() {
            return Ok(d.path.clone());
        }
    }

    let esp: Vec<_> = devices
        .iter()
        .filter(|d| d.vid == Some(ESP_VID) && d.pid == Some(ESP_PID_JTAG))
        .collect();
    if esp.len() == 1 {
        return Ok(esp[0].path.clone());
    }
    if esp.len() > 1 {
        return Ok(esp[0].path.clone());
    }
    if devices.len() == 1 {
        return Ok(devices[0].path.clone());
    }
    if !devices.is_empty() {
        if let Some(d) = devices.iter().find(|d| d.path == "/dev/tty-buddy") {
            return Ok(d.path.clone());
        }
        return Ok(devices[0].path.clone());
    }
    anyhow::bail!("no serial device available")
}
