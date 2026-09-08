# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Initial public surface of tty-buddy: ESP32-C3 status/console buddy with a
Rust host daemon.

### Added

- ESP32-C3 SuperMini firmware (PlatformIO) driving a 320×240 ST7789:
  status UI, 53×30 VT mirror, and a one-button OSD (mode, brightness, sleep)
- Rust host daemon: live metrics → status UI, PTY console for `shell_user`,
  USB keyboard grab, auto-reconnect over USB-JTAG serial
- Framed StatusSnap protocol (through v13) with configurable layout, colors,
  interfaces, systemd services, and a bottom alert strip
- `[osd]` in `status.config`: brightness/sleep, alert dismiss, wake-on-alert,
  optional day/night auto brightness
- Bidirectional brightness/sleep sync with the host config; last values kept
  in device NVS when the daemon is offline; keyboard activity wakes sleep in
  terminal mode
- Hardware: KiCad schematic, wiring notes, and enclosure CAD
- Linux packaging: systemd unit, udev rule (`/dev/tty-buddy`), install
  tarball; `install.sh` binds the service and `shell_user` to the installing
  user and owns `status.config` for OSD writeback
- Merged factory firmware image (bootloader + partitions + app) plus flash
  helpers; root `Makefile` for local `firmware` / `daemon` / `release`
  artifacts
- CI for daemon and firmware; draft GitHub Releases on matching `v*` tags

[Unreleased]: https://github.com/imflawlezz/tty-buddy/commits/main
