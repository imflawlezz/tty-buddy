//! Serial open + framed write with ACK/NAK retries.

use std::io::{Read, Write};
use std::time::{Duration, Instant};

use anyhow::{Context, Result};
use serialport::{ClearBuffer, SerialPort};

use crate::protocol::{
    build_frame, DEV_MODE_TOGGLE, DEV_OSD_BRIGHT, DEV_OSD_SLEEP, FRAME_ACK, FRAME_NAK, PAYLOAD_LEN,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DeviceEvent {
    ModeToggle,
    /// 0 = auto, 1..=6 = step.
    Brightness(u8),
    /// 0 = never, 1..=6 = level.
    Sleep(u8),
}

pub struct BuddySerial {
    port: Box<dyn SerialPort>,
    seq: u8,
    inbox: Vec<u8>,
}

impl BuddySerial {
    pub fn open(path: &str) -> Result<Self> {
        let port = serialport::new(path, 115_200)
            .timeout(Duration::from_millis(50))
            .open()
            .with_context(|| format!("open serial {path}"))?;
        let mut s = Self {
            port,
            seq: 0,
            inbox: Vec::new(),
        };
        // Discard CDC boot garbage before the first framed write.
        std::thread::sleep(Duration::from_millis(600));
        s.port.clear(ClearBuffer::All).ok();
        let mut buf = [0u8; 512];
        while s.port.read(&mut buf).ok().filter(|&n| n > 0).is_some() {}
        Ok(s)
    }

    pub fn next_seq(&mut self) -> u8 {
        let s = self.seq;
        self.seq = self.seq.wrapping_add(1);
        s
    }

    fn pump_inbox(&mut self) {
        let mut buf = [0u8; 256];
        loop {
            match self.port.read(&mut buf) {
                Ok(0) => break,
                Ok(n) => self.inbox.extend_from_slice(&buf[..n]),
                Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => break,
                Err(_) => break,
            }
        }
    }

    fn is_device_opcode(b: u8) -> bool {
        matches!(b, DEV_MODE_TOGGLE | DEV_OSD_BRIGHT | DEV_OSD_SLEEP)
    }

    pub fn poll_events(&mut self) -> Vec<DeviceEvent> {
        self.pump_inbox();
        let mut out = Vec::new();
        let mut i = 0;
        while i < self.inbox.len() {
            match self.inbox[i] {
                DEV_MODE_TOGGLE => {
                    out.push(DeviceEvent::ModeToggle);
                    self.inbox.remove(i);
                }
                DEV_OSD_BRIGHT => {
                    if i + 1 >= self.inbox.len() {
                        break;
                    }
                    let v = self.inbox[i + 1];
                    self.inbox.drain(i..=i + 1);
                    out.push(DeviceEvent::Brightness(v.min(6)));
                }
                DEV_OSD_SLEEP => {
                    if i + 1 >= self.inbox.len() {
                        break;
                    }
                    let v = self.inbox[i + 1];
                    self.inbox.drain(i..=i + 1);
                    out.push(DeviceEvent::Sleep(v.min(6)));
                }
                _ => i += 1,
            }
        }
        out
    }

    fn wait_ack(&mut self, seq: u8, timeout: Duration) -> Result<bool> {
        let deadline = Instant::now() + timeout;
        while Instant::now() < deadline {
            self.pump_inbox();
            let mut i = 0;
            while i + 1 < self.inbox.len() {
                let a = self.inbox[i];
                let b = self.inbox[i + 1];
                if a == FRAME_ACK && b == seq {
                    self.inbox.drain(..=i + 1);
                    return Ok(true);
                }
                if a == FRAME_NAK && b == seq {
                    self.inbox.drain(..=i + 1);
                    return Ok(false);
                }
                if Self::is_device_opcode(a) {
                    // Defer to poll_events; OSD opcodes need the following data byte.
                    if a == DEV_MODE_TOGGLE || i + 1 < self.inbox.len() {
                        i += if a == DEV_MODE_TOGGLE { 1 } else { 2 };
                    } else {
                        break;
                    }
                    continue;
                }
                self.inbox.remove(i);
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        Ok(false)
    }

    pub fn send_frame(
        &mut self,
        cx: u8,
        cy: u8,
        flags: u8,
        payload: &[u8; PAYLOAD_LEN],
    ) -> Result<()> {
        let seq = self.next_seq();
        let frame = build_frame(seq, cx, cy, flags, payload);
        let mut last_err = None;
        for attempt in 0..5 {
            let mut off = 0;
            while off < frame.len() {
                let end = (off + 64).min(frame.len());
                match self.port.write_all(&frame[off..end]) {
                    Ok(()) => off = end,
                    Err(e) => {
                        last_err = Some(e);
                        break;
                    }
                }
                // Chunked write: ESP32-C3 USB-JTAG overruns on large bursts.
                std::thread::sleep(Duration::from_micros(200));
            }
            if last_err.is_some() {
                std::thread::sleep(Duration::from_millis(30 * (attempt + 1) as u64));
                last_err = None;
                continue;
            }
            self.port.flush().ok();
            match self.wait_ack(seq, Duration::from_millis(600))? {
                true => return Ok(()),
                false => {
                    std::thread::sleep(Duration::from_millis(25 * (attempt + 1) as u64));
                }
            }
        }
        anyhow::bail!("no ACK for seq {seq}")
    }
}
