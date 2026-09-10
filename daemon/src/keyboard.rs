//! USB keyboard → PTY bytes via Linux evdev (hotplug via [`Keyboard::maintain`]).

use std::fs::File;
use std::os::fd::AsRawFd;
use std::path::{Path, PathBuf};

use anyhow::Result;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum KeyboardLayout {
    #[default]
    Us,
    Pl,
    De,
}

impl KeyboardLayout {
    pub fn parse(s: &str) -> Option<Self> {
        match s.trim().to_ascii_lowercase().as_str() {
            "us" | "en" | "qwerty" => Some(Self::Us),
            "pl" | "pl_qwertz" | "polish" => Some(Self::Pl),
            "de" | "de_qwertz" | "german" => Some(Self::De),
            _ => None,
        }
    }
}

#[derive(Default)]
pub struct Keyboard {
    devices: Vec<Opened>,
    allowlist: Vec<String>,
    layout: KeyboardLayout,
    shift: bool,
    ctrl: bool,
    alt: bool,
    enabled: bool,
    listening: bool,
    exclusive: bool,
    warned_empty_allowlist: bool,
    warned_permission: bool,
}

struct Opened {
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
            allowlist: Vec::new(),
            layout: KeyboardLayout::Us,
            shift: false,
            ctrl: false,
            alt: false,
            enabled: true,
            listening: false,
            exclusive: false,
            warned_empty_allowlist: false,
            warned_permission: false,
        })
    }

    pub fn configure(&mut self, layout: KeyboardLayout, allowlist: Vec<String>) {
        self.layout = layout;
        self.allowlist = allowlist;
        if self.enabled && self.allowlist.is_empty() && !self.warned_empty_allowlist {
            eprintln!(
                "keyboard: WARNING: [behavior] keyboard_devices is empty — \
                 no keyboards will be opened. Set paths (/dev/input/by-id/…) \
                 or name substrings to allowlist devices before grab/watch."
            );
            self.warned_empty_allowlist = true;
        }
    }

    pub fn grab(&mut self) -> Result<()> {
        if !self.enabled {
            return Ok(());
        }
        self.listening = true;
        self.exclusive = true;
        self.maintain()
    }

    /// Open without EVIOCGRAB (status-mode activity watch).
    pub fn watch(&mut self) -> Result<()> {
        if !self.enabled {
            return Ok(());
        }
        self.listening = true;
        self.exclusive = false;
        for g in &mut self.devices {
            if g.grabbed {
                let _ = evdev_ungrab(g.file.as_raw_fd());
                g.grabbed = false;
            }
        }
        self.maintain()
    }

    pub fn ungrab(&mut self) {
        self.listening = false;
        self.exclusive = false;
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

    pub fn maintain(&mut self) -> Result<()> {
        if !self.enabled || !self.listening {
            return Ok(());
        }

        let found = discover_keyboards(&self.allowlist, &mut self.warned_permission);

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
                    set_nonblock(fd);
                    let mut grabbed = false;
                    if self.exclusive {
                        match evdev_grab(fd) {
                            Ok(()) => {
                                grabbed = true;
                                eprintln!("keyboard: grabbed {} ({})", path.display(), name.trim());
                            }
                            Err(e) => {
                                eprintln!("keyboard: grab {} failed: {e}", path.display());
                            }
                        }
                    } else {
                        eprintln!("keyboard: watch {} ({})", path.display(), name.trim());
                    }
                    self.devices.push(Opened {
                        path,
                        name,
                        file,
                        grabbed,
                    });
                }
                Err(e) => {
                    if e.kind() == std::io::ErrorKind::PermissionDenied {
                        if !self.warned_permission {
                            log_input_permission(&path, &e);
                            self.warned_permission = true;
                        }
                    } else {
                        eprintln!("keyboard: open {} failed: {e}", path.display());
                    }
                }
            }
        }

        for g in &mut self.devices {
            if self.exclusive && !g.grabbed {
                if evdev_grab(g.file.as_raw_fd()).is_ok() {
                    g.grabbed = true;
                    eprintln!("keyboard: re-grabbed {} ({})", g.path.display(), g.name);
                }
            } else if !self.exclusive && g.grabbed {
                let _ = evdev_ungrab(g.file.as_raw_fd());
                g.grabbed = false;
            }
        }

        Ok(())
    }

    pub fn grabbed_count(&self) -> usize {
        self.devices.iter().filter(|g| g.grabbed).count()
    }

    pub fn device_count(&self) -> usize {
        self.devices.len()
    }

    pub fn poll(&mut self) -> Vec<Vec<u8>> {
        let mut out = Vec::new();
        if !self.enabled || !self.listening {
            return out;
        }

        let mut dead: Vec<usize> = Vec::new();
        for (idx, g) in self.devices.iter().enumerate() {
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
                if let Some(bytes) = keycode_to_bytes(self.layout, code, self.shift, self.ctrl) {
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

fn discover_keyboards(
    allowlist: &[String],
    warned_permission: &mut bool,
) -> Vec<(PathBuf, String)> {
    let mut out = Vec::new();
    if allowlist.is_empty() {
        return out;
    }

    let path_entries: Vec<&str> = allowlist
        .iter()
        .map(|s| s.as_str())
        .filter(|s| s.starts_with('/'))
        .collect();
    let name_entries: Vec<String> = allowlist
        .iter()
        .filter(|s| !s.starts_with('/'))
        .map(|s| s.to_ascii_lowercase())
        .collect();

    for entry in &path_entries {
        let path = PathBuf::from(entry);
        if !path.exists() {
            continue;
        }
        let resolved = std::fs::canonicalize(&path).unwrap_or_else(|_| path.clone());
        let name = evdev_name_for_path(&resolved).unwrap_or_else(|| entry.to_string());
        if out.iter().any(|(p, _)| p == &resolved) {
            continue;
        }
        out.push((resolved, name));
    }

    if name_entries.is_empty() {
        out.sort_by(|a, b| a.0.cmp(&b.0));
        return out;
    }

    let rd = match std::fs::read_dir("/dev/input") {
        Ok(rd) => rd,
        Err(e) => {
            if e.kind() == std::io::ErrorKind::PermissionDenied && !*warned_permission {
                log_input_permission(Path::new("/dev/input"), &e);
                *warned_permission = true;
            }
            out.sort_by(|a, b| a.0.cmp(&b.0));
            return out;
        }
    };
    for e in rd.flatten() {
        let fname = e.file_name();
        let n = fname.to_string_lossy();
        if !n.starts_with("event") {
            continue;
        }
        let path = e.path();
        let phys = std::fs::read_to_string(format!("/sys/class/input/{n}/device/name"))
            .unwrap_or_default()
            .to_lowercase();
        if is_denylisted_name(&phys) {
            continue;
        }
        if !name_entries.iter().any(|sub| phys.contains(sub)) {
            continue;
        }
        if out.iter().any(|(p, _)| p == &path) {
            continue;
        }
        out.push((path, phys.trim().to_string()));
    }
    out.sort_by(|a, b| a.0.cmp(&b.0));
    out
}

fn log_input_permission(path: &Path, err: &std::io::Error) {
    eprintln!(
        "keyboard: {err} accessing {} — permission denied (EACCES). \
         Add the daemon user to the `input` group (packaging uses dialout,input) \
         and log out/in (or reboot) so the new group applies.",
        path.display()
    );
}

fn is_denylisted_name(phys: &str) -> bool {
    phys.contains("power")
        || phys.contains("lid")
        || phys.contains("video bus")
        || phys.contains("consumer control")
        || phys.contains("system control")
        || phys.contains("mouse")
}

fn evdev_name_for_path(path: &Path) -> Option<String> {
    let n = path.file_name()?.to_str()?;
    std::fs::read_to_string(format!("/sys/class/input/{n}/device/name"))
        .ok()
        .map(|s| s.trim().to_string())
}

/// Linux `EVIOCGRAB` = `_IOW('E', 0x90, int)`.
fn eviocgrab() -> libc::c_ulong {
    // Match linux/ioctl.h: _IOC(dir, type, nr, size) with SIZEBITS=14.
    const NRBITS: u32 = 8;
    const TYPEBITS: u32 = 8;
    const SIZEBITS: u32 = 14;
    const NRSHIFT: u32 = 0;
    const TYPESHIFT: u32 = NRSHIFT + NRBITS;
    const SIZESHIFT: u32 = TYPESHIFT + TYPEBITS;
    const DIRSHIFT: u32 = SIZESHIFT + SIZEBITS;
    const WRITE: u32 = 1;
    ((WRITE as libc::c_ulong) << DIRSHIFT)
        | ((b'E' as libc::c_ulong) << TYPESHIFT)
        | ((0x90_u32 as libc::c_ulong) << NRSHIFT)
        | ((std::mem::size_of::<libc::c_int>() as libc::c_ulong) << SIZESHIFT)
}

fn set_nonblock(fd: i32) {
    let flags = unsafe { libc::fcntl(fd, libc::F_GETFL) };
    if flags >= 0 {
        unsafe {
            libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK);
        }
    }
}

fn evdev_grab(fd: i32) -> Result<()> {
    let grab: i32 = 1;
    let rc = unsafe { libc::ioctl(fd, eviocgrab(), &grab as *const i32 as *const _) };
    if rc < 0 {
        anyhow::bail!("{}", std::io::Error::last_os_error());
    }
    set_nonblock(fd);
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

pub(crate) fn keycode_to_bytes(
    layout: KeyboardLayout,
    code: u16,
    shift: bool,
    ctrl: bool,
) -> Option<Vec<u8>> {
    match code {
        1 => return Some(vec![0x1b]),
        14 => return Some(vec![0x7f]),
        15 => return Some(vec![b'\t']),
        28 => return Some(vec![b'\r']),
        103 => return Some(b"\x1b[A".to_vec()),
        108 => return Some(b"\x1b[B".to_vec()),
        106 => return Some(b"\x1b[C".to_vec()),
        105 => return Some(b"\x1b[D".to_vec()),
        _ => {}
    }

    let (normal, shifted) = printable_pair(layout, code)?;
    let bytes = if shift { shifted } else { normal };
    if ctrl && bytes.len() == 1 {
        let c = bytes[0].to_ascii_lowercase();
        if c.is_ascii_lowercase() {
            return Some(vec![c & 0x1f]);
        }
    }
    Some(bytes.to_vec())
}

/// Unshifted/shifted UTF-8 for one printable keycode.
fn printable_pair(layout: KeyboardLayout, code: u16) -> Option<(&'static [u8], &'static [u8])> {
    // QWERTZ (pl/de) swaps physical KEY_Y / KEY_Z.
    let letter = |us: &'static [u8]| -> Option<(&'static [u8], &'static [u8])> {
        let upper = match us {
            b"a" => b"A".as_slice(),
            b"b" => b"B",
            b"c" => b"C",
            b"d" => b"D",
            b"e" => b"E",
            b"f" => b"F",
            b"g" => b"G",
            b"h" => b"H",
            b"i" => b"I",
            b"j" => b"J",
            b"k" => b"K",
            b"l" => b"L",
            b"m" => b"M",
            b"n" => b"N",
            b"o" => b"O",
            b"p" => b"P",
            b"q" => b"Q",
            b"r" => b"R",
            b"s" => b"S",
            b"t" => b"T",
            b"u" => b"U",
            b"v" => b"V",
            b"w" => b"W",
            b"x" => b"X",
            b"y" => b"Y",
            b"z" => b"Z",
            _ => return None,
        };
        Some((us, upper))
    };

    match code {
        57 => Some((b" ", b" ")),
        16 => letter(b"q"),
        17 => letter(b"w"),
        18 => letter(b"e"),
        19 => letter(b"r"),
        20 => letter(b"t"),
        21 => match layout {
            KeyboardLayout::Us => letter(b"y"),
            KeyboardLayout::Pl | KeyboardLayout::De => letter(b"z"),
        },
        22 => letter(b"u"),
        23 => letter(b"i"),
        24 => letter(b"o"),
        25 => letter(b"p"),
        30 => letter(b"a"),
        31 => letter(b"s"),
        32 => letter(b"d"),
        33 => letter(b"f"),
        34 => letter(b"g"),
        35 => letter(b"h"),
        36 => letter(b"j"),
        37 => letter(b"k"),
        38 => letter(b"l"),
        44 => match layout {
            KeyboardLayout::Us => letter(b"z"),
            KeyboardLayout::Pl | KeyboardLayout::De => letter(b"y"),
        },
        45 => letter(b"x"),
        46 => letter(b"c"),
        47 => letter(b"v"),
        48 => letter(b"b"),
        49 => letter(b"n"),
        50 => letter(b"m"),
        // US/PL digit+punct; DE uses German symbols.
        2..=13 | 26 | 27 | 39..=41 | 43 | 51..=53 => match layout {
            KeyboardLayout::Us | KeyboardLayout::Pl => us_symbol(code),
            KeyboardLayout::De => de_symbol(code),
        },
        _ => None,
    }
}

fn us_symbol(code: u16) -> Option<(&'static [u8], &'static [u8])> {
    Some(match code {
        2 => (b"1", b"!"),
        3 => (b"2", b"@"),
        4 => (b"3", b"#"),
        5 => (b"4", b"$"),
        6 => (b"5", b"%"),
        7 => (b"6", b"^"),
        8 => (b"7", b"&"),
        9 => (b"8", b"*"),
        10 => (b"9", b"("),
        11 => (b"0", b")"),
        12 => (b"-", b"_"),
        13 => (b"=", b"+"),
        26 => (b"[", b"{"),
        27 => (b"]", b"}"),
        39 => (b";", b":"),
        40 => (b"'", b"\""),
        41 => (b"`", b"~"),
        43 => (b"\\", b"|"),
        51 => (b",", b"<"),
        52 => (b".", b">"),
        53 => (b"/", b"?"),
        _ => return None,
    })
}

fn de_symbol(code: u16) -> Option<(&'static [u8], &'static [u8])> {
    Some(match code {
        2 => (b"1", b"!"),
        3 => (b"2", b"\""),
        4 => (b"3", "§".as_bytes()),
        5 => (b"4", b"$"),
        6 => (b"5", b"%"),
        7 => (b"6", b"&"),
        8 => (b"7", b"/"),
        9 => (b"8", b"("),
        10 => (b"9", b")"),
        11 => (b"0", b"="),
        12 => ("ß".as_bytes(), b"?"),
        13 => ("´".as_bytes(), b"`"),
        26 => ("ü".as_bytes(), "Ü".as_bytes()),
        27 => (b"+", b"*"),
        39 => ("ö".as_bytes(), "Ö".as_bytes()),
        40 => ("ä".as_bytes(), "Ä".as_bytes()),
        41 => (b"^", "°".as_bytes()),
        43 => (b"#", b"'"),
        51 => (b",", b";"),
        52 => (b".", b":"),
        53 => (b"-", b"_"),
        _ => return None,
    })
}

pub(crate) fn parse_keyboard_devices(s: &str) -> Vec<String> {
    s.split(',')
        .map(|p| p.trim().to_string())
        .filter(|p| !p.is_empty())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn special_keys() {
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 1, false, false),
            Some(vec![0x1b])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 14, false, false),
            Some(vec![0x7f])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 15, false, false),
            Some(vec![b'\t'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 28, false, false),
            Some(vec![b'\r'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 103, false, false),
            Some(b"\x1b[A".to_vec())
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 108, false, false),
            Some(b"\x1b[B".to_vec())
        );
    }

    #[test]
    fn letters_shift_and_ctrl() {
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 30, false, false),
            Some(vec![b'a'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 30, true, false),
            Some(vec![b'A'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 30, false, true),
            Some(vec![0x01])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 16, true, false),
            Some(vec![b'Q'])
        );
    }

    #[test]
    fn y_z_swap_on_qwertz_layouts() {
        // Physical KEY_Y (21) / KEY_Z (44).
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 21, false, false),
            Some(vec![b'y'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 44, false, false),
            Some(vec![b'z'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Pl, 21, false, false),
            Some(vec![b'z'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Pl, 44, false, false),
            Some(vec![b'y'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::De, 21, true, false),
            Some(vec![b'Z'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::De, 44, true, false),
            Some(vec![b'Y'])
        );
    }

    #[test]
    fn digit_row_us_pl_vs_de() {
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 2, false, false),
            Some(vec![b'1'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 2, true, false),
            Some(vec![b'!'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 3, true, false),
            Some(vec![b'@'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Pl, 3, true, false),
            Some(vec![b'@'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::De, 3, true, false),
            Some(vec![b'"'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::De, 8, true, false),
            Some(vec![b'/'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 8, true, false),
            Some(vec![b'&'])
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::De, 12, false, false),
            Some("ß".as_bytes().to_vec())
        );
        assert_eq!(
            keycode_to_bytes(KeyboardLayout::Us, 57, false, false),
            Some(vec![b' '])
        );
        assert!(keycode_to_bytes(KeyboardLayout::Us, 999, false, false).is_none());
    }

    #[test]
    fn parse_layout_and_devices() {
        assert_eq!(KeyboardLayout::parse("us"), Some(KeyboardLayout::Us));
        assert_eq!(KeyboardLayout::parse("PL"), Some(KeyboardLayout::Pl));
        assert_eq!(KeyboardLayout::parse("de"), Some(KeyboardLayout::De));
        assert!(KeyboardLayout::parse("fr").is_none());
        assert_eq!(
            parse_keyboard_devices("/dev/input/by-id/foo, Logitech ,"),
            vec!["/dev/input/by-id/foo".to_string(), "Logitech".to_string()]
        );
    }

    #[test]
    fn eviocgrab_matches_linux_iow() {
        assert_eq!(eviocgrab(), 0x4004_4590);
    }

    #[test]
    fn allowlist_match_helpers() {
        assert!(is_denylisted_name("power button"));
        assert!(!is_denylisted_name("logitech keyboard"));
        assert!(discover_keyboards(&[], &mut false).is_empty());
    }
}
