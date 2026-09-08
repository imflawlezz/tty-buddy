//! 53×30 PTY console via portable-pty + vt100 (same grid as the ESP panel).

use std::io::{Read, Write};
use std::sync::{Arc, Mutex};

use anyhow::{Context, Result};
use portable_pty::{native_pty_system, CommandBuilder, MasterPty, PtySize};
use vt100::{Callbacks, Cell, Color, Parser, Screen};

use crate::protocol::{CELLS, COLS, PAYLOAD_LEN, ROWS};

#[derive(Default)]
struct QueryCb {
    /// Host replies for DSR / DA / window-size probes.
    replies: Vec<u8>,
}

impl Callbacks for QueryCb {
    fn unhandled_csi(
        &mut self,
        screen: &mut Screen,
        intermediates: Option<u8>,
        _i2: Option<u8>,
        params: &[&[u16]],
        c: char,
    ) {
        let p0 = params.first().and_then(|p| p.first()).copied().unwrap_or(0);
        match (intermediates, c) {
            (None, 'n') => {
                if p0 == 6 {
                    let (row, col) = screen.cursor_position();
                    let _ = write!(self.replies, "\x1b[{};{}R", row + 1, col + 1);
                } else {
                    self.replies.extend_from_slice(b"\x1b[0n");
                }
            }
            (None, 'c') => {
                self.replies.extend_from_slice(b"\x1b[?6c");
            }
            // CSI t — ncurses/nano size probes; cell geometry is 6×8 on the panel.
            (None, 't') => {
                let p1 = params.get(1).and_then(|p| p.first()).copied().unwrap_or(0);
                match p0 {
                    14 | 16 => {
                        let _ = write!(self.replies, "\x1b[4;{};{}t", ROWS * 8, COLS * 6);
                    }
                    18 => {
                        let _ = write!(self.replies, "\x1b[8;{};{}t", ROWS, COLS);
                    }
                    13 => self.replies.extend_from_slice(b"\x1b[3;0;0t"),
                    _ => {
                        let _ = p1;
                    }
                }
            }
            _ => {}
        }
    }

    fn resize(&mut self, screen: &mut Screen, request: (u16, u16)) {
        // Panel geometry is fixed; ignore app resize requests.
        let _ = request;
        screen.set_size(ROWS as u16, COLS as u16);
    }
}

pub struct PtySession {
    master: Box<dyn MasterPty + Send>,
    reader: Box<dyn Read + Send>,
    writer: Box<dyn Write + Send>,
    parser: Parser<QueryCb>,
    child: Box<dyn portable_pty::Child + Send>,
    /// Latest screen frame for the serial bridge.
    frame: Arc<Mutex<TermFrame>>,
}

pub struct TermFrame {
    pub payload: [u8; PAYLOAD_LEN],
    pub cursor: (u8, u8), // col, row
    pub hide_cursor: bool,
    pub application_cursor: bool,
}

impl Default for TermFrame {
    fn default() -> Self {
        let mut payload = [0u8; PAYLOAD_LEN];
        for i in 0..CELLS {
            payload[i * 3 + 1] = b' ';
            payload[i * 3 + 2] = 0x07; // white on black
        }
        Self {
            payload,
            cursor: (0, 0),
            hide_cursor: false,
            application_cursor: false,
        }
    }
}

impl PtySession {
    pub fn spawn_login(user: Option<&str>) -> Result<Self> {
        let pty_system = native_pty_system();
        let pair = pty_system
            .openpty(PtySize {
                rows: ROWS as u16,
                cols: COLS as u16,
                pixel_width: (COLS * 6) as u16,
                pixel_height: (ROWS * 8) as u16,
            })
            .context("open pty")?;

        let (shell, home, uname) = resolve_shell(user);
        let mut cmd = CommandBuilder::new(&shell);
        cmd.arg("-l");
        cmd.cwd(&home);
        // TERM=linux: ioctl winsize + a feature set vt100 covers well.
        cmd.env("TERM", "linux");
        cmd.env("COLUMNS", COLS.to_string());
        cmd.env("LINES", ROWS.to_string());
        cmd.env("USER", &uname);
        cmd.env("LOGNAME", &uname);
        cmd.env("HOME", &home);
        cmd.env("SHELL", &shell);
        cmd.env("TTY_BUDDY", "1");

        let child = pair.slave.spawn_command(cmd).context("spawn shell")?;
        let reader = pair.master.try_clone_reader().context("clone reader")?;
        let writer = pair.master.take_writer().context("take writer")?;

        #[cfg(unix)]
        if let Some(fd) = pair.master.as_raw_fd() {
            let flags = unsafe { libc::fcntl(fd, libc::F_GETFL) };
            if flags >= 0 {
                unsafe {
                    libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK);
                }
            }
        }

        // Some shells race the initial TIOCSWINSZ; set it again after spawn.
        pair.master
            .resize(PtySize {
                rows: ROWS as u16,
                cols: COLS as u16,
                pixel_width: (COLS * 6) as u16,
                pixel_height: (ROWS * 8) as u16,
            })
            .ok();

        eprintln!("console: {uname} {shell} @ {COLS}x{ROWS} (cwd {home})");

        Ok(Self {
            master: pair.master,
            reader,
            writer,
            parser: Parser::new_with_callbacks(ROWS as u16, COLS as u16, 0, QueryCb::default()),
            child,
            frame: Arc::new(Mutex::new(TermFrame::default())),
        })
    }

    /// Pin PTY winsize to the panel (SIGWINCH for TUIs after mode switches).
    pub fn force_resize(&mut self) {
        let size = PtySize {
            rows: ROWS as u16,
            cols: COLS as u16,
            pixel_width: (COLS * 6) as u16,
            pixel_height: (ROWS * 8) as u16,
        };
        if let Err(e) = self.master.resize(size) {
            eprintln!("console: resize failed: {e}");
        }
        self.parser.screen_mut().set_size(ROWS as u16, COLS as u16);
    }

    pub fn frame(&self) -> Arc<Mutex<TermFrame>> {
        self.frame.clone()
    }

    pub fn pump(&mut self) -> Result<()> {
        let mut buf = [0u8; 8192];
        loop {
            match self.reader.read(&mut buf) {
                Ok(0) => break,
                Ok(n) => {
                    self.parser.process(&buf[..n]);
                    let replies = std::mem::take(&mut self.parser.callbacks_mut().replies);
                    if !replies.is_empty() {
                        let _ = self.write_input(&replies);
                    }
                }
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => break,
                Err(e) => return Err(e.into()),
            }
        }
        self.sync_frame();
        Ok(())
    }

    fn sync_frame(&mut self) {
        let screen = self.parser.screen();
        let mut frame = self.frame.lock().unwrap();
        screen_to_payload(screen, &mut frame.payload);
        let (row, col) = screen.cursor_position();
        frame.cursor = (
            col.min((COLS - 1) as u16) as u8,
            row.min((ROWS - 1) as u16) as u8,
        );
        frame.hide_cursor = screen.hide_cursor();
        frame.application_cursor = screen.application_cursor();
    }

    pub fn write_input(&mut self, data: &[u8]) -> Result<()> {
        self.writer.write_all(data)?;
        self.writer.flush()?;
        Ok(())
    }

    pub fn try_wait(&mut self) -> bool {
        matches!(self.child.try_wait(), Ok(Some(_)))
    }

    pub fn application_cursor(&self) -> bool {
        self.frame.lock().unwrap().application_cursor
    }
}

fn screen_to_payload(screen: &Screen, out: &mut [u8; PAYLOAD_LEN]) {
    for row in 0..ROWS {
        for col in 0..COLS {
            let i = (row * COLS + col) * 3;
            let Some(cell) = screen.cell(row as u16, col as u16) else {
                out[i] = 0;
                out[i + 1] = b' ';
                out[i + 2] = 0x07;
                continue;
            };
            let (cp, attr) = cell_to_cp_attr(cell);
            out[i] = (cp >> 8) as u8;
            out[i + 1] = (cp & 0xFF) as u8;
            out[i + 2] = attr;
        }
    }
}

fn cell_to_cp_attr(cell: &Cell) -> (u16, u8) {
    let contents = cell.contents();
    let cp = if contents.is_empty() {
        b' ' as u16
    } else {
        let ch = contents.chars().next().unwrap_or(' ');
        let u = ch as u32;
        if u <= 0xFFFF {
            u as u16
        } else {
            b'?' as u16
        }
    };

    let mut fg = color_to_ansi(cell.fgcolor(), true);
    let mut bg = color_to_ansi(cell.bgcolor(), false);
    if cell.bold() && fg < 8 {
        fg += 8;
    }
    if cell.inverse() {
        std::mem::swap(&mut fg, &mut bg);
    }
    let attr = (fg & 0x0F) | ((bg & 0x0F) << 4);
    (cp, attr)
}

fn color_to_ansi(c: Color, is_fg: bool) -> u8 {
    match c {
        Color::Default => {
            if is_fg {
                7
            } else {
                0
            }
        }
        Color::Idx(i) => i & 0x0F,
        Color::Rgb(r, g, b) => rgb_to_ansi16(r, g, b),
    }
}

pub(crate) fn rgb_to_ansi16(r: u8, g: u8, b: u8) -> u8 {
    let palette: [(u8, u8, u8); 16] = [
        (0, 0, 0),
        (170, 0, 0),
        (0, 170, 0),
        (170, 85, 0),
        (0, 0, 170),
        (170, 0, 170),
        (0, 170, 170),
        (170, 170, 170),
        (85, 85, 85),
        (255, 85, 85),
        (85, 255, 85),
        (255, 255, 85),
        (85, 85, 255),
        (255, 85, 255),
        (85, 255, 255),
        (255, 255, 255),
    ];
    let mut best = 7u8;
    let mut best_d = u32::MAX;
    for (i, (pr, pg, pb)) in palette.iter().enumerate() {
        let dr = r as i32 - *pr as i32;
        let dg = g as i32 - *pg as i32;
        let db = b as i32 - *pb as i32;
        let d = (dr * dr + dg * dg + db * db) as u32;
        if d < best_d {
            best_d = d;
            best = i as u8;
        }
    }
    best
}

fn resolve_shell(user: Option<&str>) -> (String, String, String) {
    let uname = user
        .map(str::to_string)
        .or_else(|| std::env::var("USER").ok())
        .unwrap_or_else(|| "dih".into());

    if let Ok(ents) = std::fs::read_to_string("/etc/passwd") {
        for line in ents.lines() {
            let mut p = line.split(':');
            let name = p.next().unwrap_or("");
            if name != uname {
                continue;
            }
            let _pw = p.next();
            let _uid = p.next();
            let _gid = p.next();
            let _gecos = p.next();
            let home = p.next().unwrap_or("/tmp");
            let shell = p.next().unwrap_or("/bin/bash");
            if !shell.is_empty() && shell != "/usr/sbin/nologin" && shell != "/bin/false" {
                return (shell.to_string(), home.to_string(), uname);
            }
            return ("/bin/bash".into(), home.to_string(), uname);
        }
    }

    let shell = std::env::var("SHELL").unwrap_or_else(|_| "/bin/bash".into());
    let home = std::env::var("HOME").unwrap_or_else(|_| format!("/home/{uname}"));
    (shell, home, uname)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rgb_maps_to_nearest_ansi16() {
        assert_eq!(rgb_to_ansi16(0, 0, 0), 0);
        assert_eq!(rgb_to_ansi16(255, 255, 255), 15);
        assert_eq!(rgb_to_ansi16(170, 0, 0), 1);
        assert_eq!(rgb_to_ansi16(0, 0, 170), 4);
        assert_eq!(rgb_to_ansi16(255, 85, 85), 9);
    }
}
