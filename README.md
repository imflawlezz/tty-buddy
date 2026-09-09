# tty-buddy

ESP32-C3 + ST7789 display buddy. Host daemon is Rust.

| Path | Contents |
|------|----------|
| [`firmware/`](firmware/) | ESP32 firmware (PlatformIO) |
| [`daemon/`](daemon/) | Rust host daemon + packaging |
| [`hardware/`](hardware/) | Wiring / CAD |

## What it does

- **Status mode** (default): live host metrics → LCD (`buddy.config`)
- **Console mode**: Linux PTY for `shell_user`
  - USB keyboard on the **server** is grabbed into that console
  - Screen is mirrored to the ESP (53×30 VT)

ESP32-C3 has no USB-host keyboard port — keyboard plugs into the Linux box.

**Button:** tap opens OSD (Mode / Bright / Sleep); long-press toggles mode when idle, or cycles the selected setting when OSD is open. Brightness and sleep sync with `[display]` in `buddy.config` (and NVS offline).

## Quick start

Wire the board ([`hardware/wiring.md`](hardware/wiring.md)), then:

1. Flash firmware (merged factory image at `0x0` — not the app-only `.pio` bin):

   ```bash
   make firmware
   cd firmware && ./scripts/flash.sh dist/tty-buddy-firmware.bin
   ```

2. Install daemon tarball: `sudo ./install.sh` (binds to the user who ran sudo)
3. Plug in (or replug) the ESP → `/dev/tty-buddy` via udev

```bash
systemctl status tty-buddy
```

Several boards on one host: `tty-buddy setup --config /etc/tty-buddy/daemon.toml`

## Build

Version: [`daemon/Cargo.toml`](daemon/Cargo.toml). Notes: [`CHANGELOG.md`](CHANGELOG.md).

```bash
make firmware   # dist/tty-buddy-firmware-<ver>.bin
make daemon     # dist/tty-buddy-<ver>-x86_64-linux.tar.gz
make release    # both
```

Tag `vX.Y.Z` matching Cargo.toml to open a draft GitHub Release.
