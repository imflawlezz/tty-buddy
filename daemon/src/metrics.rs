//! Live Linux host metrics for StatusSnap.

use std::collections::HashMap;
use std::fs;
use std::process::Command;
use std::time::{Duration, Instant};

use anyhow::Result;
use chrono::Local;

use crate::protocol::{
    StatusIface, StatusSnap, StatusSvc, ST_F_HAS_CPU, ST_F_HAS_DISK, ST_F_HAS_MEM, ST_F_HAS_TEMP,
    ST_SVC_ACTIVATING, ST_SVC_ACTIVE, ST_SVC_DEACTIVATING, ST_SVC_FAILED, ST_SVC_INACTIVE,
    ST_SVC_MAINTENANCE, ST_SVC_RELOADING, SVC_COUNT,
};
use crate::status_config::{IpMode, StatusUiConfig};

pub struct MetricsCollector {
    prev_cpu: Option<(u64, u64)>,
    prev_net: HashMap<String, (Instant, u64, u64)>,
    net_rates: HashMap<String, (u32, u32)>,
}

impl MetricsCollector {
    pub fn new() -> Self {
        Self {
            prev_cpu: None,
            prev_net: HashMap::new(),
            net_rates: HashMap::new(),
        }
    }

    pub fn sample(&mut self, cfg: &StatusUiConfig) -> Result<StatusSnap> {
        let mut snap = StatusSnap::default();
        snap.style = cfg.style.clone();

        let host = hostname();
        snap.hostname = host.split('.').next().unwrap_or(&host).to_string();

        let now = Local::now();
        snap.date = strftime_chrono(&cfg.date_format, &now);
        snap.time = strftime_chrono(&cfg.time_format, &now);

        if let Ok(load) = loadavg() {
            snap.load_x100 = [
                (load.0 * 100.0).round().clamp(0.0, 65535.0) as u16,
                (load.1 * 100.0).round().clamp(0.0, 65535.0) as u16,
                (load.2 * 100.0).round().clamp(0.0, 65535.0) as u16,
            ];
        }
        snap.uptime_sec = uptime_sec().unwrap_or(0);

        if let Some(cpu) = self.cpu_percent() {
            snap.flags |= ST_F_HAS_CPU;
            snap.cpu_pct = cpu.round().clamp(0.0, 100.0) as u8;
        }
        if let Some((used, total, pct)) = mem_info() {
            snap.flags |= ST_F_HAS_MEM;
            snap.mem_used_mb = used;
            snap.mem_total_mb = total;
            snap.mem_pct = pct;
        }
        if let Some((used, total, pct)) = swap_info() {
            snap.swap_used_mb = used;
            snap.swap_total_mb = total;
            snap.swap_pct = if total == 0 { 255 } else { pct };
        }
        if let Some((used, total, pct)) = disk_info(&cfg.disk_mount) {
            snap.flags |= ST_F_HAS_DISK;
            snap.disk_used_mb = used;
            snap.disk_total_mb = total;
            snap.disk_pct = pct;
        }
        if let Some(t10) = cpu_temp_c10() {
            snap.flags |= ST_F_HAS_TEMP;
            snap.cpu_temp_c10 = t10;
        }

        snap.ifaces = self.collect_ifaces(&cfg.interfaces);
        snap.services = collect_services(cfg.services_filter.as_deref());

        Ok(snap)
    }

    fn cpu_percent(&mut self) -> Option<f32> {
        let (idle, total) = read_cpu_times()?;
        let out = if let Some((pi, pt)) = self.prev_cpu {
            let di = idle.saturating_sub(pi) as f64;
            let dt = total.saturating_sub(pt) as f64;
            if dt > 0.0 {
                Some((1.0 - di / dt) as f32 * 100.0)
            } else {
                None
            }
        } else {
            None
        };
        self.prev_cpu = Some((idle, total));
        out
    }

    fn collect_ifaces(&mut self, wanted: &[(String, IpMode)]) -> Vec<StatusIface> {
        if wanted.is_empty() {
            return Vec::new();
        }
        let stats = read_netdev();
        let now = Instant::now();
        let mut out = Vec::new();
        for (name, mode) in wanted.iter().take(16) {
            let (rx, tx) = stats.get(name).copied().unwrap_or((0, 0));
            if let Some((pt, prx, ptx)) = self.prev_net.get(name) {
                let dt = now.duration_since(*pt).as_secs_f64().max(0.001);
                let rbps = ((rx.saturating_sub(*prx)) as f64 / dt) as u32;
                let tbps = ((tx.saturating_sub(*ptx)) as f64 / dt) as u32;
                self.net_rates.insert(name.clone(), (rbps, tbps));
            }
            self.prev_net.insert(name.clone(), (now, rx, tx));
            let (rx_bps, tx_bps) = self.net_rates.get(name).copied().unwrap_or((0, 0));
            let ip = iface_ip(name, *mode).unwrap_or_default();
            out.push(StatusIface {
                name: name.clone(),
                ip,
                rx_bps,
                tx_bps,
            });
        }
        out
    }
}

fn hostname() -> String {
    fs::read_to_string("/etc/hostname")
        .map(|s| s.trim().to_string())
        .unwrap_or_else(|_| {
            Command::new("hostname")
                .output()
                .ok()
                .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
                .unwrap_or_else(|| "host".into())
        })
}

fn strftime_chrono(fmt: &str, t: &chrono::DateTime<Local>) -> String {
    // status.config uses C/Python-style % tokens; map the common subset to chrono.
    let mut out = String::new();
    let mut chars = fmt.chars().peekable();
    while let Some(c) = chars.next() {
        if c != '%' {
            out.push(c);
            continue;
        }
        let Some(spec) = chars.next() else {
            out.push('%');
            break;
        };
        let piece = match spec {
            'Y' => t.format("%Y").to_string(),
            'y' => t.format("%y").to_string(),
            'm' => t.format("%m").to_string(),
            'd' => t.format("%d").to_string(),
            'H' => t.format("%H").to_string(),
            'I' => t.format("%I").to_string(),
            'M' => t.format("%M").to_string(),
            'S' => t.format("%S").to_string(),
            'b' => t.format("%b").to_string(),
            'B' => t.format("%B").to_string(),
            'a' => t.format("%a").to_string(),
            'A' => t.format("%A").to_string(),
            'p' => t.format("%p").to_string(),
            '%' => "%".into(),
            other => format!("%{other}"),
        };
        out.push_str(&piece);
    }
    out
}

fn loadavg() -> Result<(f32, f32, f32)> {
    let s = fs::read_to_string("/proc/loadavg")?;
    let mut parts = s.split_whitespace();
    let a: f32 = parts.next().unwrap_or("0").parse()?;
    let b: f32 = parts.next().unwrap_or("0").parse()?;
    let c: f32 = parts.next().unwrap_or("0").parse()?;
    Ok((a, b, c))
}

fn uptime_sec() -> Result<u32> {
    let s = fs::read_to_string("/proc/uptime")?;
    let sec: f64 = s.split_whitespace().next().unwrap_or("0").parse()?;
    Ok(sec as u32)
}

fn read_cpu_times() -> Option<(u64, u64)> {
    let s = fs::read_to_string("/proc/stat").ok()?;
    let line = s.lines().next()?;
    if !line.starts_with("cpu ") {
        return None;
    }
    let nums: Vec<u64> = line
        .split_whitespace()
        .skip(1)
        .filter_map(|x| x.parse().ok())
        .collect();
    if nums.len() < 4 {
        return None;
    }
    let idle = nums[3] + nums.get(4).copied().unwrap_or(0); // idle + iowait
    let total: u64 = nums.iter().sum();
    Some((idle, total))
}

fn mem_info() -> Option<(u32, u32, u8)> {
    let s = fs::read_to_string("/proc/meminfo").ok()?;
    let mut total_kb = 0u64;
    let mut avail_kb = 0u64;
    for line in s.lines() {
        if line.starts_with("MemTotal:") {
            total_kb = line.split_whitespace().nth(1)?.parse().ok()?;
        } else if line.starts_with("MemAvailable:") {
            avail_kb = line.split_whitespace().nth(1)?.parse().ok()?;
        }
    }
    if total_kb == 0 {
        return None;
    }
    let used_kb = total_kb.saturating_sub(avail_kb);
    let pct = ((used_kb as f64 / total_kb as f64) * 100.0).round() as u8;
    Some(((used_kb / 1024) as u32, (total_kb / 1024) as u32, pct.min(100)))
}

fn swap_info() -> Option<(u32, u32, u8)> {
    let s = fs::read_to_string("/proc/meminfo").ok()?;
    let mut total_kb = 0u64;
    let mut free_kb = 0u64;
    for line in s.lines() {
        if line.starts_with("SwapTotal:") {
            total_kb = line.split_whitespace().nth(1)?.parse().ok()?;
        } else if line.starts_with("SwapFree:") {
            free_kb = line.split_whitespace().nth(1)?.parse().ok()?;
        }
    }
    if total_kb == 0 {
        return Some((0, 0, 255));
    }
    let used_kb = total_kb.saturating_sub(free_kb);
    let pct = ((used_kb as f64 / total_kb as f64) * 100.0).round() as u8;
    Some(((used_kb / 1024) as u32, (total_kb / 1024) as u32, pct.min(100)))
}

fn disk_info(mount: &str) -> Option<(u32, u32, u8)> {
    let mut st: libc::statvfs = unsafe { std::mem::zeroed() };
    let c = std::ffi::CString::new(mount).ok()?;
    let rc = unsafe { libc::statvfs(c.as_ptr(), &mut st) };
    if rc != 0 {
        return None;
    }
    let frsize = st.f_frsize as u64;
    let total = st.f_blocks.saturating_mul(frsize);
    let avail = st.f_bavail.saturating_mul(frsize);
    let used = total.saturating_sub(avail);
    if total == 0 {
        return None;
    }
    let pct = ((used as f64 / total as f64) * 100.0).round() as u8;
    Some((
        (used / (1024 * 1024)) as u32,
        (total / (1024 * 1024)) as u32,
        pct.min(100),
    ))
}

fn cpu_temp_c10() -> Option<i16> {
    let zones = fs::read_dir("/sys/class/thermal").ok()?;
    let mut best: Option<i64> = None;
    for e in zones.flatten() {
        let path = e.path();
        let name = path.file_name()?.to_string_lossy().to_string();
        if !name.starts_with("thermal_zone") {
            continue;
        }
        let typ = fs::read_to_string(path.join("type")).ok()?;
        let typ = typ.trim();
        let temp: i64 = fs::read_to_string(path.join("temp")).ok()?.trim().parse().ok()?;
        if typ.contains("x86_pkg") || typ.contains("cpu") || typ.contains("soc") {
            return Some((temp / 100) as i16); // millidegree → c10
        }
        if best.is_none() {
            best = Some(temp);
        }
    }
    best.map(|t| (t / 100) as i16)
}

fn read_netdev() -> HashMap<String, (u64, u64)> {
    let mut map = HashMap::new();
    let Ok(s) = fs::read_to_string("/proc/net/dev") else {
        return map;
    };
    for line in s.lines().skip(2) {
        let line = line.trim();
        let Some((name, rest)) = line.split_once(':') else {
            continue;
        };
        let name = name.trim().to_string();
        let cols: Vec<&str> = rest.split_whitespace().collect();
        if cols.len() < 9 {
            continue;
        }
        let rx: u64 = cols[0].parse().unwrap_or(0);
        let tx: u64 = cols[8].parse().unwrap_or(0);
        map.insert(name, (rx, tx));
    }
    map
}

fn iface_ip(name: &str, mode: IpMode) -> Option<String> {
    let out = Command::new("ip")
        .args(["-o", if mode == IpMode::V6 { "-6" } else { "-4" }, "addr", "show", "dev", name])
        .output()
        .ok()?;
    let text = String::from_utf8_lossy(&out.stdout);
    for line in text.lines() {
        let mut parts = line.split_whitespace();
        while let Some(p) = parts.next() {
            if p == "inet" || p == "inet6" {
                if let Some(addr) = parts.next() {
                    let addr = addr.split('/').next().unwrap_or(addr);
                    if mode == IpMode::V6 && addr.starts_with("fe80:") {
                        continue;
                    }
                    return Some(addr.to_string());
                }
            }
        }
    }
    // No global address: accept link-local as last resort.
    for line in text.lines() {
        let mut parts = line.split_whitespace();
        while let Some(p) = parts.next() {
            if p == "inet" || p == "inet6" {
                if let Some(addr) = parts.next() {
                    return Some(addr.split('/').next().unwrap_or(addr).to_string());
                }
            }
        }
    }
    None
}

fn normalize_svc_status(active: &str) -> u8 {
    match active.trim().to_ascii_lowercase().as_str() {
        "active" => ST_SVC_ACTIVE,
        "failed" => ST_SVC_FAILED,
        "inactive" => ST_SVC_INACTIVE,
        "activating" => ST_SVC_ACTIVATING,
        "deactivating" => ST_SVC_DEACTIVATING,
        "reloading" => ST_SVC_RELOADING,
        "maintenance" => ST_SVC_MAINTENANCE,
        _ => ST_SVC_MAINTENANCE,
    }
}

fn collect_services(filter: Option<&[String]>) -> Vec<StatusSvc> {
    let out = Command::new("systemctl")
        .args([
            "list-units",
            "--type=service",
            "--all",
            "--no-legend",
            "--no-pager",
            "--plain",
        ])
        .output();
    let Ok(out) = out else {
        return Vec::new();
    };
    let text = String::from_utf8_lossy(&out.stdout);
    let mut services = Vec::new();
    for line in text.lines() {
        let cols: Vec<&str> = line.split_whitespace().collect();
        if cols.len() < 4 {
            continue;
        }
        let unit = cols[0];
        if !unit.ends_with(".service") {
            continue;
        }
        let name = unit.trim_end_matches(".service");
        if name.ends_with('@') {
            continue;
        }
        if let Some(f) = filter {
            if !f.iter().any(|x| x == name || x == unit) {
                continue;
            }
        }
        // systemctl --plain: UNIT LOAD ACTIVE SUB …
        let active = cols[2];
        services.push(StatusSvc {
            name: name.chars().take(12).collect(),
            status: normalize_svc_status(active),
        });
        if services.len() >= SVC_COUNT {
            break;
        }
    }
    // Unfiltered: surface failed/transitioning units before a long active list.
    if filter.is_none() {
        services.sort_by_key(|s| match s.status {
            ST_SVC_FAILED => 0,
            ST_SVC_ACTIVATING | ST_SVC_RELOADING | ST_SVC_DEACTIVATING => 1,
            ST_SVC_ACTIVE => 2,
            ST_SVC_INACTIVE => 3,
            _ => 4,
        });
        services.truncate(SVC_COUNT);
    }
    services
}

#[allow(dead_code)]
fn _duration_unused() {
    let _ = Duration::from_secs(1);
}
