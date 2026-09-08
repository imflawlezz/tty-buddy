//! USB keyboard → PTY bytes via Linux evdev (hotplug via [`Keyboard::maintain`]).

use std::fs::File;
use std::os::fd::AsRawFd;
use std::path::PathBuf;

use anyhow::Result;

#[derive(Default)]
pub struct Keyboard {
    devices: Vec<Grabbed>,
    shift: bool,
    ctrl: bool,
    alt: bool,
    enabled: bool,
    /// Exclusive grab while console mode is active.
    want_grab: bool,
}

struct Grabbed {
    path: PathBuf,
    name: String,
    file: File,
    grabbed: bool,
}

impl Keyboard {
    pub fn disabled() -> Self {
        Self::default()
    }

    pub fn open() -> Result<Self> {
        Ok(Self {
            devices: Vec::new(),
            shift: false,
            ctrl: false,
            alt: false,
            enabled: true,
            want_grab: false,
        })
    }

    /// Enter console grab mode and attach current keyboards.
    pub fn grab(&mut self) -> Result<()> {
        if !self.enabled {
            return Ok(());
        }
        self.want_grab = true;
        self.maintain()
    }

    /// Leave console grab mode and release all devices.
    pub fn ungrab(&mut self) {
        self.want_grab = false;
        for g in &mut self.devices {
            if g.grabbed {
                let _ = evdev_ungrab(g.file.as_raw_fd());
                g.grabbed = false;
            }
        }
        self.devices.clear();
        self.shift = false;
        self.ctrl = false;
        self.alt = false;
    }

    /// Attach new keyboards and drop removed ones. Intended ~1 Hz while grabbed.
    pub fn maintain(&mut self) -> Result<()> {
        if !self.enabled || !self.want_grab {
            return Ok(());
        }

        let found = discover_keyboards();

        self.devices.retain(|g| {
            let keep = found.iter().any(|(p, _)| p == &g.path) && g.path.exists();
            if !keep && g.grabbed {
                let _ = evdev_ungrab(g.file.as_raw_fd());
                eprintln!("keyboard: removed {}", g.path.display());
            }
            keep
        });

        for (path, name) in found {
            if self.devices.iter().any(|d| d.path == path) {
                continue;
            }
            match File::options().read(true).write(true).open(&path) {
                Ok(file) => {
                    let fd = file.as_raw_fd();
                    match evdev_grab(fd) {
                        Ok(()) => {
                            eprintln!("keyboard: grabbed {} ({})", path.display(), name.trim());
                            self.devices.push(Grabbed {
                                path,
                                name,
                                file,
                                grabbed: true,
                            });
                        }
                        Err(e) => {
                            eprintln!("keyboard: grab {} failed: {e}", path.display());
                        }
                    }
                }
                Err(e) => {
                    // Node often appears before permissions settle; next maintain retries.
                    if e.kind() != std::io::ErrorKind::PermissionDenied {
                        eprintln!("keyboard: open {} failed: {e}", path.display());
                    }
                }
            }
        }

        for g in &mut self.devices {
            if !g.grabbed && evdev_grab(g.file.as_raw_fd()).is_ok() {
                g.grabbed = true;
                eprintln!("keyboard: re-grabbed {} ({})", g.path.display(), g.name);
            }
        }

        Ok(())
    }

    pub fn grabbed_count(&self) -> usize {
        self.devices.iter().filter(|g| g.grabbed).count()
    }

    pub fn poll(&mut self) -> Vec<Vec<u8>> {
        let mut out = Vec::new();
        if !self.enabled || !self.want_grab {
            return out;
        }

        let mut dead: Vec<usize> = Vec::new();
        for (idx, g) in self.devices.iter().enumerate() {
            if !g.grabbed {
                continue;
            }
            loop {
                let mut buf = [0u8; 24];
                let n = unsafe {
                    libc::read(g.file.as_raw_fd(), buf.as_mut_ptr() as *mut _, buf.len())
                };
                if n < 0 {
                    let err = std::io::Error::last_os_error();
                    if err.kind() == std::io::ErrorKind::WouldBlock
                        || err.raw_os_error() == Some(libc::EAGAIN)
                    {
                        break;
                    }
                    dead.push(idx);
                    break;
                }
                if n != 24 {
                    break;
                }
                let typ = u16::from_ne_bytes([buf[16], buf[17]]);
                let code = u16::from_ne_bytes([buf[18], buf[19]]);
                let value = i32::from_ne_bytes([buf[20], buf[21], buf[22], buf[23]]);
                if typ != 1 {
                    continue;
                }
                match code {
                    42 | 54 => {
                        self.shift = value != 0;
                        continue;
                    }
                    29 | 97 => {
                        self.ctrl = value != 0;
                        continue;
                    }
                    56 | 100 => {
                        self.alt = value != 0;
                        continue;
                    }
                    _ => {}
                }
                if value != 1 && value != 2 {
                    continue;
                }
                if let Some(bytes) = keycode_to_bytes(code, self.shift, self.ctrl) {
                    out.push(bytes);
                }
            }
        }

        if !dead.is_empty() {
            dead.sort_unstable();
            dead.dedup();
            for idx in dead.into_iter().rev() {
                let g = self.devices.remove(idx);
                eprintln!("keyboard: lost {}", g.path.display());
            }
            let _ = self.maintain();
        }

        out
    }
}

impl Drop for Keyboard {
    fn drop(&mut self) {
        self.ungrab();
    }
}

fn discover_keyboards() -> Vec<(PathBuf, String)> {
    let mut out = Vec::new();
    let Ok(rd) = std::fs::read_dir("/dev/input") else {
        return out;
    };
    for e in rd.flatten() {
        let name = e.file_name();
        let n = name.to_string_lossy();
        if !n.starts_with("event") {
            continue;
        }
        let path = e.path();
        let phys = std::fs::read_to_string(format!("/sys/class/input/{n}/device/name"))
            .unwrap_or_default()
            .to_lowercase();
        if phys.contains("power")
            || phys.contains("lid")
            || phys.contains("video bus")
            || phys.contains("consumer control")
            || phys.contains("system control")
            || phys.contains("mouse")
        {
            continue;
        }
        if !(phys.contains("keyboard") || phys.contains("kbd")) {
            continue;
        }
        out.push((path, phys.trim().to_string()));
    }
    out.sort_by(|a, b| a.0.cmp(&b.0));
    out
}

fn eviocgrab() -> libc::c_ulong {
    // EVIOCGRAB = _IOW('E', 0x90, int) on Linux x86_64
    0x4004_4590
}

fn evdev_grab(fd: i32) -> Result<()> {
    let grab: i32 = 1;
    let rc = unsafe { libc::ioctl(fd, eviocgrab(), &grab as *const i32 as *const _) };
    if rc < 0 {
        anyhow::bail!("{}", std::io::Error::last_os_error());
    }
    let flags = unsafe { libc::fcntl(fd, libc::F_GETFL) };
    if flags >= 0 {
        unsafe {
            libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK);
        }
    }
    Ok(())
}

fn evdev_ungrab(fd: i32) -> Result<()> {
    let grab: i32 = 0;
    let rc = unsafe { libc::ioctl(fd, eviocgrab(), &grab as *const i32 as *const _) };
    if rc < 0 {
        anyhow::bail!("{}", std::io::Error::last_os_error());
    }
    Ok(())
}

pub(crate) fn keycode_to_bytes(code: u16, shift: bool, ctrl: bool) -> Option<Vec<u8>> {
    let ch = match code {
        1 => return Some(vec![0x1b]),
        14 => return Some(vec![0x7f]),
        15 => return Some(vec![b'\t']),
        28 => return Some(vec![b'\r']),
        103 => return Some(b"\x1b[A".to_vec()),
        108 => return Some(b"\x1b[B".to_vec()),
        106 => return Some(b"\x1b[C".to_vec()),
        105 => return Some(b"\x1b[D".to_vec()),
        57 => b' ',
        2 => {
            if shift {
                b'!'
            } else {
                b'1'
            }
        }
        3 => {
            if shift {
                b'@'
            } else {
                b'2'
            }
        }
        4 => {
            if shift {
                b'#'
            } else {
                b'3'
            }
        }
        5 => {
            if shift {
                b'$'
            } else {
                b'4'
            }
        }
        6 => {
            if shift {
                b'%'
            } else {
                b'5'
            }
        }
        7 => {
            if shift {
                b'^'
            } else {
                b'6'
            }
        }
        8 => {
            if shift {
                b'&'
            } else {
                b'7'
            }
        }
        9 => {
            if shift {
                b'*'
            } else {
                b'8'
            }
        }
        10 => {
            if shift {
                b'('
            } else {
                b'9'
            }
        }
        11 => {
            if shift {
                b')'
            } else {
                b'0'
            }
        }
        12 => {
            if shift {
                b'_'
            } else {
                b'-'
            }
        }
        13 => {
            if shift {
                b'+'
            } else {
                b'='
            }
        }
        16 => letter(b'q', shift),
        17 => letter(b'w', shift),
        18 => letter(b'e', shift),
        19 => letter(b'r', shift),
        20 => letter(b't', shift),
        21 => letter(b'y', shift),
        22 => letter(b'u', shift),
        23 => letter(b'i', shift),
        24 => letter(b'o', shift),
        25 => letter(b'p', shift),
        30 => letter(b'a', shift),
        31 => letter(b's', shift),
        32 => letter(b'd', shift),
        33 => letter(b'f', shift),
        34 => letter(b'g', shift),
        35 => letter(b'h', shift),
        36 => letter(b'j', shift),
        37 => letter(b'k', shift),
        38 => letter(b'l', shift),
        44 => letter(b'z', shift),
        45 => letter(b'x', shift),
        46 => letter(b'c', shift),
        47 => letter(b'v', shift),
        48 => letter(b'b', shift),
        49 => letter(b'n', shift),
        50 => letter(b'm', shift),
        39 => {
            if shift {
                b':'
            } else {
                b';'
            }
        }
        40 => {
            if shift {
                b'"'
            } else {
                b'\''
            }
        }
        41 => {
            if shift {
                b'~'
            } else {
                b'`'
            }
        }
        43 => {
            if shift {
                b'|'
            } else {
                b'\\'
            }
        }
        51 => {
            if shift {
                b'<'
            } else {
                b','
            }
        }
        52 => {
            if shift {
                b'>'
            } else {
                b'.'
            }
        }
        53 => {
            if shift {
                b'?'
            } else {
                b'/'
            }
        }
        26 => {
            if shift {
                b'{'
            } else {
                b'['
            }
        }
        27 => {
            if shift {
                b'}'
            } else {
                b']'
            }
        }
        _ => return None,
    };
    if ctrl {
        let c = ch.to_ascii_lowercase();
        if c.is_ascii_lowercase() {
            return Some(vec![c & 0x1f]);
        }
    }
    Some(vec![ch])
}

fn letter(c: u8, shift: bool) -> u8 {
    if shift {
        c.to_ascii_uppercase()
    } else {
        c
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn special_keys() {
        assert_eq!(keycode_to_bytes(1, false, false), Some(vec![0x1b]));
        assert_eq!(keycode_to_bytes(14, false, false), Some(vec![0x7f]));
        assert_eq!(keycode_to_bytes(15, false, false), Some(vec![b'\t']));
        assert_eq!(keycode_to_bytes(28, false, false), Some(vec![b'\r']));
        assert_eq!(
            keycode_to_bytes(103, false, false),
            Some(b"\x1b[A".to_vec())
        );
        assert_eq!(
            keycode_to_bytes(108, false, false),
            Some(b"\x1b[B".to_vec())
        );
    }

    #[test]
    fn letters_shift_and_ctrl() {
        assert_eq!(keycode_to_bytes(30, false, false), Some(vec![b'a']));
        assert_eq!(keycode_to_bytes(30, true, false), Some(vec![b'A']));
        assert_eq!(keycode_to_bytes(30, false, true), Some(vec![0x01])); // Ctrl-A
        assert_eq!(keycode_to_bytes(16, true, false), Some(vec![b'Q']));
    }

    #[test]
    fn digits_and_symbols() {
        assert_eq!(keycode_to_bytes(2, false, false), Some(vec![b'1']));
        assert_eq!(keycode_to_bytes(2, true, false), Some(vec![b'!']));
        assert_eq!(keycode_to_bytes(57, false, false), Some(vec![b' ']));
        assert!(keycode_to_bytes(999, false, false).is_none());
    }
}
