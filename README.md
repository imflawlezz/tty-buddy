# tty-buddy

ESP32-C3 + ST7789 display buddy. Host daemon is Rust.

| Path | Contents |
|------|----------|
| [`firmware/`](firmware/) | ESP32 firmware (PlatformIO) |
| [`daemon/`](daemon/) | Rust host daemon + packaging |
| [`hardware/`](hardware/) | Wiring / CAD |

## What it does

- **Status mode** (default): live host metrics → LCD (`status.config`)
- **Console mode** (long-press): Linux PTY for `shell_user`
  - USB keyboard on the **server** is grabbed into that console
  - Screen is mirrored to the ESP (53×30 VT)

ESP32-C3 has no USB-host keyboard port — keyboard plugs into the Linux box.
