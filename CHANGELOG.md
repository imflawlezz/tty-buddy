# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `[behavior] keyboard_devices` allowlist: comma-separated `/dev/input/by-id/…`
  paths and/or case-insensitive name substrings; empty list opens **no**
  keyboards (watch or grab) and logs a warning
- `[behavior] keyboard_layout` (`us` / `pl` / `de`) for daemon evdev→PTY mapping
  (not XKB; no AltGr). `pl` is QWERTZ Y/Z with a US digit row; `de` emits
  German letters from the key map
- Firmware terminal glyphs for curated Polish and German diacritics (6×8), with
  a generator script and native unit tests

### Changed

- Host keyboards are no longer auto-discovered by name heuristics; configure
  `keyboard_devices` after install or USB keys stay idle
- Buddy-config reload reapplies keyboard allowlist/layout in both status
  (watch/ungrab) and terminal (re-grab)
- Docs and README cover allowlist, layouts, fail-closed serial discovery, and
  PL/DE display glyphs
- Shipped default `keyboard_opens_terminal` is `false` (kiosk / dedicated
  console: set `true`); EACCES on `/dev/input` logs an `input` group hint

### Fixed

- `daemon.toml` `serial` pin is fail-closed: an unmatched serial no longer
  falls through to another board

## [1.0.1] — 2026-09-09

### Changed

- Status metrics still refresh about once per second from `/proc`, but `ip` and
  `systemctl` results are cached for a few seconds to cut host CPU use
- Shipped `buddy.config` leaves `[interfaces]` empty (add only the ifaces you
  want shown); packaging only rewrites `shell_user = "REPLACE_ME"`
- Docs and README spell out keyboard watch/grab behaviour and the
  `wake_on_alert` sleep policy (alert holds sleep off until it clears; OSD
  idle-close still runs)

### Fixed

- Invalid or unreadable `daemon.toml` aborts `run` / `setup` instead of
  silently falling back to defaults
- `shell_user` must match the `tty-buddy@<user>` runtime user; a mismatch
  fails startup (the daemon never switches uid — it runs `$SHELL -l` as that
  user)
- Device presence checks the configured path (and that discovery still
  resolves to it), not “any Espressif CDC still exists”
- Reversed `[hero]` `warn_at` / `crit_at` values are swapped after parse
- Serial inbox drops non-opcode noise and caps growth; three consecutive
  framed-send failures drop the session again (idle ticks no longer reset
  the fail counter)
- `FLAG_STYLE` updates refresh status chrome while already in status mode

### Removed

- Packaging special-case for author `shell_user = "dih"`

## [1.0.0] — 2026-09-09

First public release: ESP32-C3 + ST7789 status/console buddy with a Linux
host daemon over USB-Serial/JTAG.

### Added

- **Status mode** on a 320×240 panel: hostname and clock, CPU / MEM / DISK
  meters with warn/crit colours, optional secondary fields (uptime, load,
  swap), network interfaces, systemd service list, and a bottom alert strip
  (CPU/MEM/DISK/temp/service)
- **Terminal mode**: 53×30 VT mirror of a login shell in a PTY for `shell_user`, with
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
  `buddy_config` path, and `shell_user` for the daemon runtime user
- **Linux packaging** for amd64 and arm64: `.deb` and portable tarballs,
  `tty-buddy@<user>` systemd unit, udev rule for `/dev/tty-buddy`,
  `install.sh` / `uninstall.sh` (`--purge`); install binds groups and
  `buddy.config` ownership to the installing user
- **Factory firmware** image (`tty-buddy-firmware-<ver>.bin`) for a full
  flash at offset `0x0`, plus Web Serial and `flash.sh` workflows

### Fixed

- OSD idle auto-close no longer stalls while `wake_on_alert` holds the panel
  awake for a status alert (sleep still waits until the alert clears)

[Unreleased]: https://github.com/imflawlezz/tty-buddy/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/imflawlezz/tty-buddy/releases/tag/v1.0.1
[1.0.0]: https://github.com/imflawlezz/tty-buddy/releases/tag/v1.0.0
