# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] — 2026-09-09

First public release: ESP32-C3 + ST7789 status/console buddy with a Linux
host daemon over USB-Serial/JTAG.

### Added

- **Status mode** on a 320×240 panel: hostname and clock, CPU / MEM / DISK
  meters with warn/crit colours, optional secondary fields (uptime, load,
  swap), network interfaces, systemd service list, and a bottom alert strip
  (CPU/MEM/DISK/temp/service)
- **Terminal mode**: 53×30 VT mirror of a login PTY for `shell_user`, with
  box/block/Braille glyphs; USB keyboard on the host is watched in status
  (optional open-to-terminal) and grabbed into the console in terminal mode
- **On-device OSD** (one button): toggle mode, brightness steps or auto
  day/night, sleep timeout, dismiss alert on tap, wake-on-alert; brightness
  and sleep sync to the host and persist in NVS when the daemon is offline
- **`buddy.config`** panel INI: `[behavior]` (`startup_mode`,
  `keyboard_opens_terminal`, `fps`), layout/colours, interfaces, services
  filter, alerts, and `[display]` (legacy `status.config` / `[osd]` still
  accepted; install can migrate the filename)
- **`daemon.toml`** host settings: device path / USB id / serial,
  `buddy_config` path, and `shell_user`
- **Linux packaging** for amd64 and arm64: `.deb` and portable tarballs,
  `tty-buddy@<user>` systemd unit, udev rule for `/dev/tty-buddy`,
  `install.sh` / `uninstall.sh` (`--purge`); install binds groups and
  `buddy.config` ownership to the installing user
- **Factory firmware** image (`tty-buddy-firmware-<ver>.bin`) for a full
  flash at offset `0x0`, plus Web Serial and `flash.sh` workflows

### Fixed

- OSD idle auto-close no longer stalls while `wake_on_alert` holds the panel
  awake for a status alert (sleep still waits until the alert clears)

[Unreleased]: https://github.com/imflawlezz/tty-buddy/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/imflawlezz/tty-buddy/releases/tag/v1.0.0
