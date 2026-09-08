//! Wire protocol: CRC frames and StatusSnap v12.

pub const COLS: usize = 53;
pub const ROWS: usize = 30;
pub const CELLS: usize = COLS * ROWS;
pub const PAYLOAD_LEN: usize = CELLS * 3; // 4770
pub const FRAME_LEN: usize = 4 + 4 + PAYLOAD_LEN + 2; // 4780

pub const FRAME_MAGIC: [u8; 4] = [0xAA, 0x55, 0xA5, 0x5A];
pub const FRAME_ACK: u8 = 0x06;
pub const FRAME_NAK: u8 = 0x15;
pub const DEV_MODE_TOGGLE: u8 = 0x12;

pub const FLAG_CURSOR_VISIBLE: u8 = 0x01;
pub const FLAG_CURSOR_ON: u8 = 0x02;
pub const FLAG_STATUS: u8 = 0x20;
pub const FLAG_BYE: u8 = 0x80;

pub const STATUS_VER: u8 = 12;
pub const STATUS_SNAP_LEN: usize = 4458;
pub const STYLE_LEN: usize = 52;
pub const IFACE_COUNT: usize = 16;
pub const SVC_COUNT: usize = 80;
pub const SVC_NAME_LEN: usize = 40;

pub const ST_F_HAS_TEMP: u8 = 0x02;
pub const ST_F_HAS_CPU: u8 = 0x20;
pub const ST_F_HAS_MEM: u8 = 0x40;
pub const ST_F_HAS_DISK: u8 = 0x80;

pub const ST_SVC_FAILED: u8 = 0;
pub const ST_SVC_ACTIVE: u8 = 1;
pub const ST_SVC_DEACTIVATING: u8 = 2;
pub const ST_SVC_ACTIVATING: u8 = 3;
pub const ST_SVC_INACTIVE: u8 = 4;
pub const ST_SVC_MAINTENANCE: u8 = 5;
pub const ST_SVC_RELOADING: u8 = 6;

pub const SEC_NONE: u8 = 0xFF;
pub const SEC_UPTIME: u8 = 1;
pub const SEC_SWAP: u8 = 2;
pub const SEC_LOAD: u8 = 3;

pub const METER_OFF: u8 = 0;
pub const METER_ON: u8 = 1;

pub const AL_CPU: u8 = 1 << 0;
pub const AL_MEM: u8 = 1 << 1;
pub const AL_DISK: u8 = 1 << 2;
pub const AL_TEMP: u8 = 1 << 3;
pub const AL_SVC_FAILED: u8 = 1 << 4;
pub const AL_SVC_INACTIVE: u8 = 1 << 5;

pub fn crc16_ccitt(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &b in data {
        crc ^= (b as u16) << 8;
        for _ in 0..8 {
            if crc & 0x8000 != 0 {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    crc
}

pub fn build_frame(seq: u8, cx: u8, cy: u8, flags: u8, payload: &[u8; PAYLOAD_LEN]) -> Vec<u8> {
    let hdr = [seq, cx, cy, flags];
    let mut crc_buf = Vec::with_capacity(4 + PAYLOAD_LEN);
    crc_buf.extend_from_slice(&hdr);
    crc_buf.extend_from_slice(payload);
    let crc = crc16_ccitt(&crc_buf);
    let mut out = Vec::with_capacity(FRAME_LEN);
    out.extend_from_slice(&FRAME_MAGIC);
    out.push(seq);
    out.push(cx);
    out.push(cy);
    out.push(flags);
    out.extend_from_slice(payload);
    out.push((crc >> 8) as u8);
    out.push((crc & 0xFF) as u8);
    let _ = hdr;
    out
}

#[derive(Clone, Debug)]
pub struct StatusStyle {
    pub label_c: u16,
    pub bg_c: u16,
    pub host_c: u16,
    pub date_c: u16,
    pub time_c: u16,
    pub level_ok: u16,
    pub level_warn: u16,
    pub level_crit: u16,
    pub hero_cpu_c: u16,
    pub hero_mem_c: u16,
    pub hero_disk_c: u16,
    pub meter_mode: u8,
    pub warn_at: u8,
    pub crit_at: u8,
    pub sec_left: u8,
    pub sec_right: u8,
    pub sec_left_c: u16,
    pub sec_right_c: u16,
    pub svc_active: u16,
    pub svc_failed: u16,
    pub svc_deactivating: u16,
    pub svc_activating: u16,
    pub svc_reloading: u16,
    pub svc_inactive: u16,
    pub svc_maintenance: u16,
    pub alert_bg_c: u16,
    pub alert_fg_c: u16,
    /// 0 = while problem active; N = max seconds if text unchanged.
    pub alert_hold_sec: u8,
    /// `AL_*` bits; 0 = off.
    pub alert_mask: u8,
    pub alert_temp_c: u8,
}

impl Default for StatusStyle {
    fn default() -> Self {
        Self {
            label_c: rgb565(0x88, 0x88, 0x88),
            bg_c: 0,
            host_c: 0xFFFF,
            date_c: rgb565(0x88, 0x88, 0x88),
            time_c: 0xFFFF,
            level_ok: rgb565(0x33, 0xAA, 0x33),
            level_warn: rgb565(0xCC, 0xCC, 0x33),
            level_crit: rgb565(0xCC, 0x33, 0x33),
            hero_cpu_c: 0xFFFF,
            hero_mem_c: 0xFFFF,
            hero_disk_c: 0xFFFF,
            meter_mode: METER_ON,
            warn_at: 60,
            crit_at: 90,
            sec_left: SEC_NONE,
            sec_right: SEC_NONE,
            sec_left_c: 0xFFFF,
            sec_right_c: 0xFFFF,
            svc_active: rgb565(0x33, 0xAA, 0x33),
            svc_failed: rgb565(0xCC, 0x33, 0x33),
            svc_deactivating: rgb565(0xCC, 0xAA, 0x33),
            svc_activating: rgb565(0x33, 0x99, 0xCC),
            svc_reloading: rgb565(0x33, 0x99, 0xCC),
            svc_inactive: rgb565(0x88, 0x88, 0x88),
            svc_maintenance: rgb565(0x88, 0x88, 0x88),
            alert_bg_c: rgb565(0x99, 0x00, 0x00),
            alert_fg_c: 0xFFFF,
            alert_hold_sec: 0,
            alert_mask: 0,
            alert_temp_c: 80,
        }
    }
}

impl StatusStyle {
    pub fn pack(&self) -> [u8; STYLE_LEN] {
        let mut b = [0u8; STYLE_LEN];
        let mut o = 0usize;
        let put_u16 = |b: &mut [u8], o: &mut usize, v: u16| {
            b[*o..*o + 2].copy_from_slice(&v.to_le_bytes());
            *o += 2;
        };
        for v in [
            self.label_c,
            self.bg_c,
            self.host_c,
            self.date_c,
            self.time_c,
            self.level_ok,
            self.level_warn,
            self.level_crit,
            self.hero_cpu_c,
            self.hero_mem_c,
            self.hero_disk_c,
        ] {
            put_u16(&mut b, &mut o, v);
        }
        b[o] = self.meter_mode;
        b[o + 1] = self.warn_at;
        b[o + 2] = self.crit_at;
        b[o + 3] = self.sec_left;
        b[o + 4] = self.sec_right;
        o += 5;
        for v in [
            self.sec_left_c,
            self.sec_right_c,
            self.svc_active,
            self.svc_failed,
            self.svc_deactivating,
            self.svc_activating,
            self.svc_reloading,
            self.svc_inactive,
            self.svc_maintenance,
            self.alert_bg_c,
            self.alert_fg_c,
        ] {
            put_u16(&mut b, &mut o, v);
        }
        b[o] = self.alert_hold_sec;
        b[o + 1] = self.alert_mask;
        b[o + 2] = self.alert_temp_c;
        o += 3;
        debug_assert_eq!(o, STYLE_LEN);
        b
    }
}

#[derive(Clone, Debug, Default)]
pub struct StatusIface {
    pub name: String,
    pub ip: String,
    pub rx_bps: u32,
    pub tx_bps: u32,
}

#[derive(Clone, Debug, Default)]
pub struct StatusSvc {
    pub name: String,
    pub status: u8,
}

#[derive(Clone, Debug)]
pub struct StatusSnap {
    pub flags: u8,
    pub style: StatusStyle,
    pub hostname: String,
    pub date: String,
    pub time: String,
    pub load_x100: [u16; 3],
    pub uptime_sec: u32,
    pub cpu_pct: u8,
    pub mem_pct: u8,
    pub disk_pct: u8,
    pub swap_pct: u8,
    pub mem_used_mb: u32,
    pub mem_total_mb: u32,
    pub disk_used_mb: u32,
    pub disk_total_mb: u32,
    pub swap_used_mb: u32,
    pub swap_total_mb: u32,
    pub cpu_temp_c10: i16,
    pub ifaces: Vec<StatusIface>,
    pub services: Vec<StatusSvc>,
}

impl Default for StatusSnap {
    fn default() -> Self {
        Self {
            flags: 0,
            style: StatusStyle::default(),
            hostname: String::new(),
            date: String::new(),
            time: String::new(),
            load_x100: [0; 3],
            uptime_sec: 0,
            cpu_pct: 255,
            mem_pct: 255,
            disk_pct: 255,
            swap_pct: 255,
            mem_used_mb: 0,
            mem_total_mb: 0,
            disk_used_mb: 0,
            disk_total_mb: 0,
            swap_used_mb: 0,
            swap_total_mb: 0,
            cpu_temp_c10: 0x7FFF,
            ifaces: Vec::new(),
            services: Vec::new(),
        }
    }
}

fn pad_str(s: &str, n: usize) -> Vec<u8> {
    let mut raw = s.as_bytes().to_vec();
    if raw.len() > n {
        raw.truncate(n);
    }
    raw.resize(n, 0);
    raw
}

impl StatusSnap {
    pub fn pack(&self) -> [u8; STATUS_SNAP_LEN] {
        let mut out = [0u8; STATUS_SNAP_LEN];
        let mut o = 0usize;
        let put = |out: &mut [u8], o: &mut usize, bytes: &[u8]| {
            out[*o..*o + bytes.len()].copy_from_slice(bytes);
            *o += bytes.len();
        };
        put(&mut out, &mut o, b"TBST");
        put(&mut out, &mut o, &[STATUS_VER, self.flags]);
        put(&mut out, &mut o, &self.style.pack());
        put(&mut out, &mut o, &pad_str(&self.hostname, 24));
        put(&mut out, &mut o, &pad_str(&self.date, 20));
        put(&mut out, &mut o, &pad_str(&self.time, 12));
        for v in self.load_x100 {
            put(&mut out, &mut o, &v.to_le_bytes());
        }
        put(&mut out, &mut o, &self.uptime_sec.to_le_bytes());
        put(
            &mut out,
            &mut o,
            &[self.cpu_pct, self.mem_pct, self.disk_pct, self.swap_pct],
        );
        for v in [
            self.mem_used_mb,
            self.mem_total_mb,
            self.disk_used_mb,
            self.disk_total_mb,
            self.swap_used_mb,
            self.swap_total_mb,
        ] {
            put(&mut out, &mut o, &v.to_le_bytes());
        }
        put(&mut out, &mut o, &self.cpu_temp_c10.to_le_bytes());

        for i in 0..IFACE_COUNT {
            let iface = self.ifaces.get(i);
            let name = iface.map(|x| x.name.as_str()).unwrap_or("");
            let ip = iface.map(|x| x.ip.as_str()).unwrap_or("");
            let rx = iface.map(|x| x.rx_bps).unwrap_or(0);
            let tx = iface.map(|x| x.tx_bps).unwrap_or(0);
            put(&mut out, &mut o, &pad_str(name, 16));
            put(&mut out, &mut o, &pad_str(ip, 40));
            put(&mut out, &mut o, &rx.to_le_bytes());
            put(&mut out, &mut o, &tx.to_le_bytes());
        }
        for i in 0..SVC_COUNT {
            let svc = self.services.get(i);
            let name = svc.map(|x| x.name.as_str()).unwrap_or("");
            let st = svc.map(|x| x.status).unwrap_or(0);
            put(&mut out, &mut o, &pad_str(name, SVC_NAME_LEN));
            put(&mut out, &mut o, &[if name.is_empty() { 0 } else { st }]);
        }
        debug_assert_eq!(o, STATUS_SNAP_LEN);
        out
    }

    pub fn to_payload(&self) -> [u8; PAYLOAD_LEN] {
        let mut payload = [0u8; PAYLOAD_LEN];
        let snap = self.pack();
        payload[..STATUS_SNAP_LEN].copy_from_slice(&snap);
        payload
    }
}

pub fn rgb565(r: u8, g: u8, b: u8) -> u16 {
    (((r as u16) & 0xF8) << 8) | (((g as u16) & 0xFC) << 3) | ((b as u16) >> 3)
}

pub fn parse_hex_color(s: &str, default: u16) -> u16 {
    let t = s.trim();
    if t.is_empty() {
        return default;
    }
    let h = t.strip_prefix('#').unwrap_or(t);
    let h = if h.len() == 3 {
        format!(
            "{}{}{}{}{}{}",
            h.chars().next().unwrap(),
            h.chars().next().unwrap(),
            h.chars().nth(1).unwrap(),
            h.chars().nth(1).unwrap(),
            h.chars().nth(2).unwrap(),
            h.chars().nth(2).unwrap()
        )
    } else {
        h.to_string()
    };
    if h.len() != 6 {
        return default;
    }
    let Ok(r) = u8::from_str_radix(&h[0..2], 16) else {
        return default;
    };
    let Ok(g) = u8::from_str_radix(&h[2..4], 16) else {
        return default;
    };
    let Ok(b) = u8::from_str_radix(&h[4..6], 16) else {
        return default;
    };
    rgb565(r, g, b)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sizes() {
        assert_eq!(PAYLOAD_LEN, 4770);
        assert_eq!(FRAME_LEN, 4780);
        assert_eq!(STATUS_SNAP_LEN, 4458);
        assert_eq!(STYLE_LEN, 52);
        assert_eq!(StatusStyle::default().pack().len(), 52);
        assert_eq!(StatusSnap::default().pack().len(), 4458);
    }

    #[test]
    fn crc16_empty_and_known() {
        assert_eq!(crc16_ccitt(&[]), 0xFFFF);
        // CRC-CCITT (init 0xFFFF, poly 0x1021) over "123456789"
        assert_eq!(crc16_ccitt(b"123456789"), 0x29B1);
    }

    #[test]
    fn build_frame_layout_and_crc() {
        let payload = [0u8; PAYLOAD_LEN];
        let frame = build_frame(7, 10, 20, FLAG_STATUS, &payload);
        assert_eq!(frame.len(), FRAME_LEN);
        assert_eq!(&frame[0..4], &FRAME_MAGIC);
        assert_eq!(frame[4], 7);
        assert_eq!(frame[5], 10);
        assert_eq!(frame[6], 20);
        assert_eq!(frame[7], FLAG_STATUS);
        let mut crc_buf = Vec::with_capacity(4 + PAYLOAD_LEN);
        crc_buf.extend_from_slice(&[7, 10, 20, FLAG_STATUS]);
        crc_buf.extend_from_slice(&payload);
        let crc = crc16_ccitt(&crc_buf);
        assert_eq!(frame[FRAME_LEN - 2], (crc >> 8) as u8);
        assert_eq!(frame[FRAME_LEN - 1], (crc & 0xFF) as u8);
    }

    #[test]
    fn rgb565_and_hex_colors() {
        assert_eq!(rgb565(0xFF, 0x00, 0x00), 0xF800);
        assert_eq!(rgb565(0x00, 0xFF, 0x00), 0x07E0);
        assert_eq!(rgb565(0x00, 0x00, 0xFF), 0x001F);
        assert_eq!(parse_hex_color("#F00", 0), rgb565(0xFF, 0x00, 0x00));
        assert_eq!(parse_hex_color("#00FF00", 0), rgb565(0x00, 0xFF, 0x00));
        assert_eq!(parse_hex_color("888888", 1), rgb565(0x88, 0x88, 0x88));
        assert_eq!(parse_hex_color("", 0x1234), 0x1234);
        assert_eq!(parse_hex_color("nope", 0xABCD), 0xABCD);
        assert_eq!(parse_hex_color("#12", 9), 9);
    }

    #[test]
    fn style_pack_le_fields() {
        let st = StatusStyle {
            label_c: 0xABCD,
            warn_at: 42,
            crit_at: 77,
            meter_mode: METER_OFF,
            sec_left: SEC_UPTIME,
            sec_right: SEC_LOAD,
            ..Default::default()
        };
        let b = st.pack();
        assert_eq!(u16::from_le_bytes([b[0], b[1]]), 0xABCD);
        assert_eq!(b[22], METER_OFF);
        assert_eq!(b[23], 42);
        assert_eq!(b[24], 77);
        assert_eq!(b[25], SEC_UPTIME);
        assert_eq!(b[26], SEC_LOAD);
    }

    #[test]
    fn snap_pack_magic_ver_and_fields() {
        let mut snap = StatusSnap {
            flags: ST_F_HAS_CPU | ST_F_HAS_MEM,
            hostname: "host-xyz".into(),
            date: "08-09-2026".into(),
            time: "15:04:05".into(),
            load_x100: [123, 456, 789],
            uptime_sec: 0x11223344,
            cpu_pct: 10,
            mem_pct: 20,
            disk_pct: 30,
            swap_pct: 40,
            cpu_temp_c10: -150,
            ..Default::default()
        };
        snap.ifaces.push(StatusIface {
            name: "eth0".into(),
            ip: "10.0.0.1".into(),
            rx_bps: 100,
            tx_bps: 200,
        });
        snap.services.push(StatusSvc {
            name: "ssh".into(),
            status: ST_SVC_ACTIVE,
        });
        snap.services.push(StatusSvc {
            name: "x".repeat(SVC_NAME_LEN + 5),
            status: ST_SVC_FAILED,
        });

        let packed = snap.pack();
        assert_eq!(&packed[0..4], b"TBST");
        assert_eq!(packed[4], STATUS_VER);
        assert_eq!(packed[5], ST_F_HAS_CPU | ST_F_HAS_MEM);

        let style_end = 6 + STYLE_LEN;
        let host = &packed[style_end..style_end + 24];
        assert_eq!(&host[..8], b"host-xyz");
        assert!(host[8..].iter().all(|&c| c == 0));

        let date_off = style_end + 24;
        assert_eq!(&packed[date_off..date_off + 10], b"08-09-2026");
        let time_off = date_off + 20;
        assert_eq!(&packed[time_off..time_off + 8], b"15:04:05");

        let load_off = time_off + 12;
        assert_eq!(
            u16::from_le_bytes([packed[load_off], packed[load_off + 1]]),
            123
        );
        assert_eq!(
            u16::from_le_bytes([packed[load_off + 2], packed[load_off + 3]]),
            456
        );

        let up_off = load_off + 6;
        assert_eq!(
            u32::from_le_bytes(packed[up_off..up_off + 4].try_into().unwrap()),
            0x11223344
        );
        assert_eq!(&packed[up_off + 4..up_off + 8], &[10u8, 20, 30, 40]);

        let payload = snap.to_payload();
        assert_eq!(payload.len(), PAYLOAD_LEN);
        assert_eq!(&payload[..STATUS_SNAP_LEN], &packed);
        assert!(payload[STATUS_SNAP_LEN..].iter().all(|&b| b == 0));
    }

    #[test]
    fn empty_service_slot_status_is_zero() {
        let packed = StatusSnap::default().pack();
        // Services start after fixed header + 16 iface slots (64 bytes each).
        let iface_block = IFACE_COUNT * (16 + 40 + 4 + 4);
        let header = 6 + STYLE_LEN + 24 + 20 + 12 + 6 + 4 + 4 + 24 + 2;
        let svc0 = header + iface_block;
        assert_eq!(&packed[svc0..svc0 + SVC_NAME_LEN], &[0u8; SVC_NAME_LEN]);
        assert_eq!(packed[svc0 + SVC_NAME_LEN], 0);
    }
}
