# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Initial public surface of tty-buddy: ESP32-C3 status/console buddy with a
Rust host daemon.

### Added

- ESP32-C3 SuperMini firmware (PlatformIO) for ST7789 320×240: status UI,
  53×30 VT mirror (including box/block/Braille glyphs), backlight PWM, and a
  one-button OSD (mode / brightness / sleep; alert dismiss; wake-on-alert)
- Bidirectional brightness/sleep with `[osd]` in `status.config` (and device
  NVS when offline); keyboard activity wakes sleep in terminal mode; OSD
  dismiss restores only the overlay region
- Rust host daemon: live metrics → status UI, PTY console for `shell_user`,
  USB keyboard grab, auto-reconnect over USB-JTAG serial
- Framed StatusSnap protocol (through v13): layout, colors, interfaces,
  systemd services, bottom alert strip (crit/temp/service), and OSD style
  fields
- Hardware: KiCad schematic, wiring notes, and enclosure CAD
- Linux packaging: **`.deb` (amd64 / arm64)** primary install; portable
  **tarballs** with `install.sh` / `uninstall.sh` (`--purge`); systemd
  `tty-buddy@<user>` template; install binds `shell_user`, groups, and
  `status.config` ownership to the installing user
- Merged factory firmware image (`tty-buddy-firmware-<ver>.bin`, flash at
  `0x0`); root `Makefile` (`firmware` / `daemon` / `daemon-all` / `release`);
  draft GitHub Releases on `v*` tags with `.deb`s, tarballs, firmware, and
  `checksums.txt`
- CI for daemon (fmt/clippy/test) and firmware (native tests + device build)

### Changed

- `[osd] brightness` replaces `default_brightness` (legacy key still accepted)
- Host bridge is Rust + systemd (supersedes the earlier Python prototype)

[Unreleased]: https://github.com/imflawlezz/tty-buddy/commits/main
