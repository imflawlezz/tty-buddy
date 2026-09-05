#!/usr/bin/env python3
"""
PTY shell ↔ tty-buddy serial bridge.

Renders a 53×30 pyte screen and streams CRC frames to the ESP display.

Scrollback (also shown on the LCD):
  PageUp / Shift+PageUp     up half a screen
  PageDown / Shift+PageDown down half a screen
  Ctrl+Up / Ctrl+Down       one line
  Any other key             jump to live bottom, then forward to the shell

  python3 daemon/tools/tty_bridge.py -p /dev/cu.usbmodem14401
  python3 daemon/tools/tty_bridge.py -p /dev/cu.usbmodem14401 -c htop
"""

from __future__ import annotations

import argparse
import errno
import fcntl
import os
import pty
import select
import signal
import struct
import sys
import termios
import time
import tty

import pyte
from pyte.screens import HistoryScreen

COLS = 53
ROWS = 30
SCROLLBACK = 1000
PAYLOAD_LEN = COLS * ROWS * 2
FRAME_MAGIC = b"\xaa\x55\xa5\x5a"
FRAME_ACK = 0x06
FRAME_NAK = 0x15
FLAG_BYE = 0x80
# Static scrollback has no cursor blink — keepalive before device link timeout.
KEEPALIVE_S = 1.0

# Raw-mode sequences handled as scrollback (not forwarded to the PTY).
KEY_SCROLL_UP_PAGE = (
    b"\x1b[5~",  # PageUp
    b"\x1b[5;2~",  # Shift+PageUp
    b"\x1b[5;1~",
)
KEY_SCROLL_DOWN_PAGE = (
    b"\x1b[6~",  # PageDown
    b"\x1b[6;2~",  # Shift+PageDown
    b"\x1b[6;1~",
)
KEY_SCROLL_UP_LINE = (
    b"\x1b[1;5A",  # Ctrl+Up
    b"\x1b[1;2A",  # Shift+Up
)
KEY_SCROLL_DOWN_LINE = (
    b"\x1b[1;5B",  # Ctrl+Down
    b"\x1b[1;2B",  # Shift+Down
)


def crc16_ccitt(data: bytes, seed: int = 0xFFFF) -> int:
    crc = seed
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc



def set_winsize(fd: int, rows: int, cols: int) -> None:
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def open_serial(path: str) -> int:
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        attrs = termios.tcgetattr(fd)
        attrs[4] = attrs[5] = termios.B115200
        attrs[0] = 0
        attrs[1] = 0
        attrs[3] = attrs[3] & ~(
            termios.ECHO | termios.ICANON | termios.ISIG | termios.IEXTEN
        )
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except termios.error:
        pass
    return fd


def write_all(fd: int, data: bytes, chunk: int = 128) -> None:
    off = 0
    while off < len(data):
        _, w, _ = select.select([], [fd], [], 1.0)
        if not w:
            continue
        try:
            n = os.write(fd, data[off : off + chunk])
        except BlockingIOError:
            continue
        except OSError as e:
            if e.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
                continue
            raise
        if n:
            off += n
        else:
            time.sleep(0.001)


def spawn_command(cmd: list[str]) -> tuple[int, int]:
    pid, master = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.environ["COLUMNS"] = str(COLS)
        os.environ["LINES"] = str(ROWS)
        os.execvp(cmd[0], cmd)
        os._exit(127)
    set_winsize(master, ROWS, COLS)
    time.sleep(0.05)
    set_winsize(master, ROWS, COLS)
    try:
        os.kill(pid, signal.SIGWINCH)
    except ProcessLookupError:
        pass
    fl = fcntl.fcntl(master, fcntl.F_GETFL)
    fcntl.fcntl(master, fcntl.F_SETFL, fl | os.O_NONBLOCK)
    return pid, master


def color_index(color, default: int) -> int:
    if color == "default":
        return default
    named = {
        "black": 0,
        "red": 1,
        "green": 2,
        "brown": 3,
        "yellow": 3,
        "blue": 4,
        "magenta": 5,
        "cyan": 6,
        "white": 7,
        "brightblack": 8,
        "brightred": 9,
        "brightgreen": 10,
        "brightyellow": 11,
        "brightblue": 12,
        "brightmagenta": 13,
        "brightcyan": 14,
        "brightwhite": 15,
    }
    if not isinstance(color, str):
        return default
    key = color.lower().replace("-", "").replace("_", "")
    if key in named:
        return named[key]
    if key.startswith("color"):
        try:
            n = int(key[5:])
            if n < 16:
                return n
            if 16 <= n <= 231:
                return 7
            return 8 if n < 244 else 15
        except ValueError:
            return default
    hexpart = key[1:] if key.startswith("#") else key
    if len(hexpart) == 6 and all(c in "0123456789abcdef" for c in hexpart):
        r = int(hexpart[0:2], 16)
        g = int(hexpart[2:4], 16)
        b = int(hexpart[4:6], 16)
        bits = (4 if r >= 128 else 0) | (2 if g >= 128 else 0) | (1 if b >= 128 else 0)
        ansi = ((bits & 4) >> 2) | (bits & 2) | ((bits & 1) << 2)
        bright = (r + g + b) > 500
        if ansi == 0:
            return 15 if bright else 0
        return ansi + (8 if bright else 0)
    return default


def asciiize(ch: str) -> str:
    """Fold Unicode glyphs to ASCII; Font 1 only covers 0x20–0x7E."""
    if not ch:
        return " "
    o = ord(ch[0])
    if 0x20 <= o <= 0x7E:
        return ch[0]

    # Braille / block elements → density ASCII
    if 0x2800 <= o <= 0x28FF:
        dots = bin(o - 0x2800).count("1")
        return " .:-=+*#%@"[min(dots, 9)]
    if 0x2580 <= o <= 0x259F:
        blocks = {
            0x2588: "#",
            0x2589: "#",
            0x258A: "#",
            0x258B: "#",
            0x258C: "#",
            0x258D: "+",
            0x258E: "+",
            0x258F: "|",
            0x2590: "#",
            0x2591: ".",
            0x2592: "+",
            0x2593: "*",
            0x25A0: "#",
            0x25AE: "#",
            0x25FC: "#",
            0x25FE: "#",
        }
        return blocks.get(o, "|")

    # Box drawing
    if 0x2500 <= o <= 0x257F:
        if o in (0x2500, 0x2501, 0x2504, 0x2505, 0x2508, 0x2509, 0x254C, 0x254D):
            return "-"
        if o in (0x2502, 0x2503, 0x2506, 0x2507, 0x250A, 0x250B, 0x254E, 0x254F):
            return "|"
        if o in (0x250C, 0x250F, 0x2510, 0x2513, 0x2514, 0x2517, 0x2518, 0x251B,
                 0x251C, 0x2524, 0x252C, 0x2534, 0x253C, 0x2550, 0x2551, 0x2552,
                 0x2553, 0x2554, 0x2555, 0x2556, 0x2557, 0x2558, 0x2559, 0x255A,
                 0x255B, 0x255C, 0x255D, 0x255E, 0x255F, 0x2560, 0x2561, 0x2562,
                 0x2563, 0x2564, 0x2565, 0x2566, 0x2567, 0x2568, 0x2569, 0x256A,
                 0x256B, 0x256C):
            if o in (0x2550, 0x256A, 0x256B, 0x256C):
                return "="
            if o in (0x2551,):
                return "|"
            return "+"
        return "+"

    table = {
        0x00A0: " ",
        0x00B7: "*",
        0x2022: "*",
        0x2023: "*",
        0x2043: "-",
        0x2212: "-",
        0x2013: "-",
        0x2014: "-",
        0x2018: "'",
        0x2019: "'",
        0x201C: '"',
        0x201D: '"',
        0x2026: ".",
        0x2190: "<",
        0x2191: "^",
        0x2192: ">",
        0x2193: "v",
        0x21B5: "<",
        0x2264: "<",
        0x2265: ">",
        0x2260: "!",
        0x2713: "v",
        0x2714: "v",
        0x2717: "x",
        0x2718: "x",
        0x25B6: ">",
        0x25C0: "<",
        0x25B2: "^",
        0x25BC: "v",
        0x00B0: "o",
        0x2020: "+",
        0x00D7: "x",
        0x00F7: "/",
        0x03BB: "l",  # λ often appears in prompts
        0x271A: "+",
        0xFF0D: "-",
        0xFF5C: "|",
        0xFF1A: ":",
        0xFF1B: ";",
    }
    if o in table:
        return table[o]

    # Latin-1 accented → base letter when possible
    if 0xC0 <= o <= 0xFF:
        approx = (
            "AAAAAAACEEEEIIIIDNOOOOO*OUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy"
        )
        return approx[o - 0xC0]

    return "?"


def cell_to_bytes(char: str, fg, bg, bold: bool, reverse: bool) -> tuple[int, int]:
    ch = asciiize(char)
    o = ord(ch)
    if o < 32 or o > 126:
        o = ord(" ")

    fi = color_index(fg, 7)
    bi = color_index(bg, 0)
    if bold and fi < 8:
        fi += 8
    if reverse:
        fi, bi = bi, fi
    attr = (fi & 0x0F) | ((bi & 0x0F) << 4)
    return o, attr


def build_frame(
    screen: pyte.Screen, seq: int, cursor_on: bool = True
) -> bytes:
    payload = bytearray()
    for y in range(ROWS):
        row = screen.buffer[y]
        for x in range(COLS):
            cell = row[x]
            ch, attr = cell_to_bytes(
                cell.data, cell.fg, cell.bg, bool(cell.bold), bool(cell.reverse)
            )
            payload.append(ch)
            payload.append(attr)
    assert len(payload) == PAYLOAD_LEN

    cx = max(0, min(COLS - 1, int(screen.cursor.x)))
    cy = max(0, min(ROWS - 1, int(screen.cursor.y)))
    flags = 0x01  # CURSOR_VISIBLE
    hidden = bool(getattr(screen.cursor, "hidden", False))
    # HistoryScreen hides the cursor off the live bottom; mirror that for painting.
    if isinstance(screen, HistoryScreen) and not at_live_bottom(screen):
        hidden = True
    if hidden:
        flags = 0x00
    elif cursor_on:
        flags |= 0x02  # CURSOR_ON

    hdr = bytes([seq & 0xFF, cx & 0xFF, cy & 0xFF, flags & 0xFF])
    crc = crc16_ccitt(hdr + payload)
    return (
        FRAME_MAGIC
        + hdr
        + bytes(payload)
        + bytes([(crc >> 8) & 0xFF, crc & 0xFF])
    )


def drain_acks(ser: int, inbox: bytearray) -> None:
    try:
        chunk = os.read(ser, 256)
        if chunk:
            inbox.extend(chunk)
    except OSError:
        pass


def wait_ack(ser: int, inbox: bytearray, seq: int, timeout: float = 0.5) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        drain_acks(ser, inbox)
        while len(inbox) >= 2:
            code = inbox.pop(0)
            s = inbox.pop(0)
            if code == FRAME_ACK and s == (seq & 0xFF):
                return True
            if code == FRAME_NAK and s == (seq & 0xFF):
                return False
            # Unsynced bytes: discard and keep scanning for a valid pair.
        r, _, _ = select.select([ser], [], [], 0.02)
        if not r:
            continue
    return False


def send_frame(
    ser: int, inbox: bytearray, screen: pyte.Screen, seq: int, cursor_on: bool
) -> bool:
    frame = build_frame(screen, seq, cursor_on=cursor_on)
    for attempt in range(4):
        write_all(ser, frame)
        if wait_ack(ser, inbox, seq, timeout=0.4):
            return True
        time.sleep(0.02 * (attempt + 1))
    return False


def at_live_bottom(screen: HistoryScreen) -> bool:
    return screen.history.position >= screen.history.size


def reset_to_bottom(screen: HistoryScreen) -> bool:
    moved = False
    while screen.history.position < screen.history.size:
        screen.next_page()
        moved = True
    return moved


def scroll_pages(screen: HistoryScreen, direction: int) -> bool:
    """direction < 0 scrolls into history; > 0 toward the live bottom."""
    before = screen.history.position
    if direction < 0:
        screen.prev_page()
    else:
        screen.next_page()
    return screen.history.position != before


def scroll_lines(screen: HistoryScreen, direction: int, lines: int = 1) -> bool:
    old_ratio = screen.history.ratio
    screen.history = screen.history._replace(ratio=1.0 / max(screen.lines, 1))
    moved = False
    try:
        for _ in range(lines):
            before = screen.history.position
            if direction < 0:
                screen.prev_page()
            else:
                screen.next_page()
            if screen.history.position == before:
                break
            moved = True
    finally:
        screen.history = screen.history._replace(ratio=old_ratio)
    return moved


def handle_scroll_keys(key: bytes, screen: HistoryScreen) -> bool:
    """True if key was a scroll binding (do not forward to the PTY)."""
    if key in KEY_SCROLL_UP_PAGE:
        scroll_pages(screen, -1)
        return True
    if key in KEY_SCROLL_DOWN_PAGE:
        scroll_pages(screen, 1)
        return True
    if key in KEY_SCROLL_UP_LINE:
        scroll_lines(screen, -1)
        return True
    if key in KEY_SCROLL_DOWN_LINE:
        scroll_lines(screen, 1)
        return True
    return False


def send_bye(ser: int, inbox: bytearray, seq: int) -> None:
    """FLAG_BYE frame so the device shows 'lost connection' immediately."""
    payload = bytes([ord(" "), 0x07]) * (COLS * ROWS)
    flags = FLAG_BYE
    hdr = bytes([seq & 0xFF, 0, 0, flags])
    crc = crc16_ccitt(hdr + payload)
    frame = (
        FRAME_MAGIC
        + hdr
        + payload
        + bytes([(crc >> 8) & 0xFF, crc & 0xFF])
    )
    try:
        write_all(ser, frame)
        wait_ack(ser, inbox, seq, timeout=0.3)
    except OSError:
        pass


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("-p", "--port", default="/dev/cu.usbmodem14401")
    ap.add_argument("--fps", type=float, default=8.0)
    ap.add_argument(
        "-c",
        "--command",
        nargs=argparse.REMAINDER,
        help="command to run instead of $SHELL (e.g. -c htop)",
    )
    args = ap.parse_args()

    if not os.path.exists(args.port):
        print(f"Serial port not found: {args.port}", file=sys.stderr)
        return 1
    if not sys.stdin.isatty():
        print("stdin must be a TTY (run from Terminal.app / iTerm)", file=sys.stderr)
        return 1

    if args.command:
        cmd = args.command
        if cmd and cmd[0] == "--":
            cmd = cmd[1:]
    else:
        shell = os.environ.get("SHELL", "/bin/zsh")
        cmd = [shell, "-l"]

    screen = HistoryScreen(COLS, ROWS, history=SCROLLBACK, ratio=0.5)
    stream = pyte.Stream(screen)

    print(f"Opening {args.port} …", file=sys.stderr)
    ser = open_serial(args.port)
    time.sleep(0.6)
    # Discard bootloader / prior-session noise before the first frame.
    ack_inbox: bytearray = bytearray()
    drain_acks(ser, ack_inbox)
    ack_inbox.clear()
    time.sleep(0.1)
    drain_acks(ser, ack_inbox)
    ack_inbox.clear()

    print(
        f"PTY {COLS}x{ROWS} + {SCROLLBACK} lines scrollback → tty-buddy",
        file=sys.stderr,
    )
    print(f"running: {' '.join(cmd)}", file=sys.stderr)
    print(
        "Scroll: PgUp/PgDn (or Shift) · Ctrl+Up/Down for line · type to return live",
        file=sys.stderr,
    )
    print("Exit shell or Ctrl+C to stop.\n", file=sys.stderr)

    pid, master = spawn_command(cmd)

    stdin_fd = sys.stdin.fileno()
    stdout_fd = sys.stdout.fileno()
    old_stdin = termios.tcgetattr(stdin_fd)
    tty.setraw(stdin_fd)

    interval = 1.0 / max(args.fps, 0.5)
    next_frame = time.monotonic()
    last_payload = b""
    last_cursor = (-1, -1, -1)
    last_send = 0.0
    seq = 0
    screen_dirty = True

    def restore() -> None:
        termios.tcsetattr(stdin_fd, termios.TCSADRAIN, old_stdin)

    try:
        while True:
            try:
                died = os.waitpid(pid, os.WNOHANG)
                if died[0] == pid:
                    break
            except ChildProcessError:
                break

            r, _, _ = select.select([master, stdin_fd, ser], [], [], 0.02)

            if stdin_fd in r:
                try:
                    key = os.read(stdin_fd, 1024)
                except OSError:
                    key = b""
                if not key:
                    break
                if key == b"\x1c":
                    break
                if handle_scroll_keys(key, screen):
                    screen_dirty = True
                    continue
                # Typing while scrolled: return to live view before forwarding.
                if not at_live_bottom(screen):
                    if reset_to_bottom(screen):
                        screen_dirty = True
                write_all(master, key)

            if master in r:
                try:
                    data = os.read(master, 8192)
                except OSError as e:
                    if e.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
                        data = b""
                    else:
                        raise
                if not data:
                    break
                write_all(stdout_fd, data)
                stream.feed(data.decode("utf-8", errors="replace"))
                screen_dirty = True

            if ser in r:
                drain_acks(ser, ack_inbox)

            now = time.monotonic()
            cursor_on = int(now * 2) % 2 == 0  # ~2 Hz
            cx = max(0, min(COLS - 1, int(screen.cursor.x)))
            cy = max(0, min(ROWS - 1, int(screen.cursor.y)))
            hidden = bool(getattr(screen.cursor, "hidden", False))
            if isinstance(screen, HistoryScreen) and not at_live_bottom(screen):
                hidden = True
            flags = 0 if hidden else (0x01 | (0x02 if cursor_on else 0))
            cursor_state = (cx, cy, flags)
            need_keepalive = (now - last_send) >= KEEPALIVE_S

            if now >= next_frame and (
                screen_dirty or cursor_state != last_cursor or need_keepalive
            ):
                next_frame = now + interval
                frame = build_frame(screen, seq, cursor_on=cursor_on)
                payload = frame[8 : 8 + PAYLOAD_LEN]  # after magic(4)+hdr(4)
                changed = payload != last_payload or cursor_state != last_cursor
                if changed or need_keepalive:
                    if send_frame(ser, ack_inbox, screen, seq, cursor_on):
                        last_payload = payload
                        last_cursor = cursor_state
                        last_send = now
                        seq = (seq + 1) & 0xFF
                        screen_dirty = False
                else:
                    screen_dirty = False
    except KeyboardInterrupt:
        pass
    finally:
        restore()
        print("\n[tty-buddy] disconnecting daemon…", file=sys.stderr)
        try:
            send_bye(ser, ack_inbox, seq)
        except Exception:
            pass
        print("[tty-buddy] bridge stopped", file=sys.stderr)
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            os.close(master)
        except OSError:
            pass
        try:
            os.close(ser)
        except OSError:
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
