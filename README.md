# tty-buddy

A small status / console buddy for a Linux box: an **ESP32-C3** driving an
**ST7789 320×240** panel, plus a **Rust host daemon**.

**What it is for.**

Glance at host health without opening a terminal, or use the panel as a tiny
local console. Typical home-lab / desk use: CPU, memory, disk, network,
systemd units, and alerts on one side; a login shell on the other.

**What it does.**

- **Status mode** — live metrics on the LCD (hostname, clock, CPU / MEM /
  DISK, secondary fields, interfaces, services, optional alert strip).
- **Terminal mode** — 53×30 VT mirror of a PTY for a configured
  `shell_user`. A USB keyboard on the **host** (paths/names in
  `keyboard_devices`; empty = idle) is grabbed into that console in terminal
  mode.

The device has one push button for basic device controls. Short press opens
the OSD, or moves to the next row while it is open. Long press changes the
selected setting, or toggles mode when the OSD is closed.

The daemon talks to the board over USB-Serial/JTAG (`303a:1001`).

**Supported platforms**

- **Device** — ESP32-C3 SuperMini (USB-Serial/JTAG) with ST7789 **320×240**.
  Other C3 boards should work as long as the pinout matches.
- **Daemon** — Linux **amd64** and **arm64** (systemd).

Version: `1.1.0`.
License: [MIT](LICENSE).
Changes: [CHANGELOG.md](CHANGELOG.md).

| Path | Contents |
|------|----------|
| [`firmware/`](firmware/) | Device firmware (PlatformIO) |
| [`daemon/`](daemon/) | Host daemon + packaging |
| [`hardware/`](hardware/) | Schematic, wiring, enclosure |
| [`docs/`](docs/) | Flashing, daemon lifecycle, config, architecture |

---

## Getting started

**What you need**

- Wired ESP32-C3 + ST7789 ([`hardware/wiring.md`](hardware/wiring.md))
- Merged factory firmware: `tty-buddy-firmware-<version>.bin`
- Daemon package (`.deb` or tarball) from a [GitHub Release](https://github.com/imflawlezz/tty-buddy/releases) or `make release`
- Linux host for the daemon (amd64 or arm64)

### Flash the board

#### In the browser

Chrome or Edge **89+** (Web Serial). Firefox and Safari are not supported.
Use HTTPS pages (the hosted tools already are).

| Tool | URL |
|------|-----|
| Espressif esptool-js | [espressif.github.io/esptool-js](https://espressif.github.io/esptool-js/) |
| Adafruit WebSerial ESPTool | [adafruit.github.io/Adafruit_WebSerial_ESPTool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/) |

Connect the board, choose the serial port, select the factory `.bin`, flash
at offset **0x0** (full image). Steps and troubleshooting:
[`docs/guides/flashing.md`](docs/guides/flashing.md).

If a daemon is already running, free the serial port before flashing:

```bash
sudo systemctl stop 'tty-buddy@*' 'tty-buddy-board@*'
```

#### On the machine (board on USB):

```bash
# from a release artifact or make firmware → dist/
cd firmware
./scripts/flash.sh /path/to/tty-buddy-firmware-<version>.bin
```

Optional port override:

```bash
./scripts/flash.sh --port /dev/ttyACM0 /path/to/tty-buddy-firmware-<version>.bin
```

No path argument runs a PlatformIO upload at the usual offsets. `flash.sh`
prefers `/dev/tty-buddy`, then Espressif `303a:1001`.

### Install / update the daemon

Pick one packaging form. Both install the binary, udev rule
(`/dev/tty-buddy`), systemd template `tty-buddy@<user>`, set `shell_user`,
and add the user to `dialout` + `input` groups. Replug USB if the symlink is
missing; a new login may be needed for group membership to take effect.

Instance user: `TTY_BUDDY_USER`, else `SUDO_USER` (non-root). Same for
`.deb` postinst and tarball `install.sh`. Detail:
[`docs/guides/daemon.md`](docs/guides/daemon.md).

`shell_user` must match the user running `tty-buddy@<user>`. The daemon does
not switch users at runtime; it starts that user’s login shell inside the PTY.

#### `.deb` (Debian / Ubuntu)

Same command installs or upgrades. Use the package that matches the host
arch:

```bash
sudo apt-get install -y ./tty-buddy_<version>_amd64.deb
```

```bash
sudo apt-get install -y ./tty-buddy_<version>_arm64.deb
```

Check the instance unit (username = the account that ran install /
`SUDO_USER`):

```bash
systemctl status tty-buddy@$USER
```

#### Tarball (portable)

Unpack and run `install.sh` as root. On upgrade it replaces the binary,
unit, udev rule, and helpers; existing `daemon.toml` / `buddy.config` are
left alone. First install copies defaults when those files are missing.

```bash
# change `x86_64` to `aarch64` for arm64 tarball
tar xzf tty-buddy-<version>-x86_64-linux.tar.gz
cd tty-buddy-<version>-x86_64-linux
sudo ./install.sh
```

Override the instance user when needed:

```bash
# replace alice with real local username
sudo TTY_BUDDY_USER=alice ./install.sh
```

If the unit was already running, restart it after upgrade:

```bash
sudo systemctl restart tty-buddy@$USER
```

Optional check:

```bash
systemctl status tty-buddy@$USER
```

#### Several boards on one host

Single-board installs need no extra config (`/dev/tty-buddy` +
`tty-buddy@$USER`). For additional boards, udev also creates
`/dev/tty-buddy-<serial>`; register each with:

```bash
tty-buddy devices
sudo /usr/lib/tty-buddy/configure-instance.sh add-board "$USER" '<serial>'
```

That creates `/etc/tty-buddy/instances/<id>/` and enables
`tty-buddy-board@<id>`. If `serial` is set and no board matches, discovery
fails closed. Details: [daemon guide](docs/guides/daemon.md#several-boards-on-one-host).

### Keyboard on the host

USB keyboards are handled by the **daemon**, not the ESP. Prefer a stable
udev path under `/dev/input/by-id/` (the `*-event-kbd` node, not mouse /
hidraw / joystick).

```bash
ls /dev/input/by-id/
```

Allowlist that device in `buddy.config` (paths and/or case-insensitive name
substrings). **Empty `keyboard_devices` opens no keyboards** (status watch
and terminal grab both stay idle) and logs a warning. If open fails with
EACCES, ensure the instance user is in `input` and re-login. Shipped default
is `keyboard_opens_terminal = false`. For a **kiosk** / dedicated console,
set `true` so typing in status opens terminal:

```ini
[behavior]
keyboard_opens_terminal = false   # kiosk: set true
keyboard_layout = us              # us | pl | de (raw keycodes, not XKB; AltGr on pl/de)
keyboard_devices = /dev/input/by-id/usb-…-event-kbd
# or: keyboard_devices = NuPhy
```

`pl` is QWERTZ Y/Z with a US digit row; AltGr types Polish diacritics. `de`
can emit German letters and AltGr symbols. Panel glyphs for Polish/German
codepoints are firmware-side display; other scripts may show `?`.

With `keyboard_opens_terminal = true`, watched keys can open terminal from
status. While terminal mode is active, tty-buddy **EVIOCGRAB**s allowlisted
keyboards (even if auto-open is `false`), which can steal the desktop
keyboard. Detail: [`docs/guides/daemon.md`](docs/guides/daemon.md),
[`docs/reference/configuration.md`](docs/reference/configuration.md).

### Configuration

| File | Role |
|------|------|
| `/etc/tty-buddy/daemon.toml` | Device bind + `shell_user` |
| `/etc/tty-buddy/buddy.config` | Panel UX (behavior, layout, display, …) |

```ini
# Example desktop fragment (not the full shipped template)
[behavior]
startup_mode = status
keyboard_opens_terminal = false
fps = 10
keyboard_layout = us
keyboard_devices = /dev/input/by-id/usb-EXAMPLE-event-kbd

[globals]
label_color = #888888
background_color = #000000

[header]
hostname_color = #FFFFFF
date_color = #888888
time_color = #FFFFFF
date_format = %d-%m-%Y
time_format = %H:%M:%S

# ...
```

Full config reference (including display / `wake_on_alert` sleep policy):
[`docs/reference/configuration.md`](docs/reference/configuration.md).

### Uninstall

Pick one command, depending on whether you want to keep the configs for
future reinstalls. More detail: [`docs/guides/daemon.md`](docs/guides/daemon.md).

#### `.deb`

```bash
sudo apt-get remove tty-buddy
# or purge to remove configs as well
sudo apt-get purge tty-buddy
```

#### Tarball

```bash
# remove binary / unit / udev, keep configs
sudo /usr/lib/tty-buddy/uninstall.sh

# remove binary / unit / udev, delete /etc/tty-buddy
sudo /usr/lib/tty-buddy/uninstall.sh --purge
```

---

## Build and development

**Tools:** PlatformIO (`pio`) for firmware; Rust stable + `pkg-config` /
`libudev-dev` for the daemon on Linux. On macOS, `.deb` builds use Docker
when `dpkg-deb` is missing.

Release / package targets (version from `daemon/Cargo.toml` → `dist/`):

```bash
make help          # list targets
make version       # print version string
make firmware      # merged factory .bin
make daemon        # .deb + tarball (this host arch)
make daemon-all    # .deb + tarball for amd64 and arm64
make release       # firmware + daemon + checksums.txt
make release-all   # firmware + daemon-all + checksums.txt
make checksums     # SHA-256 for artifacts already in dist/
make clean         # remove dist/ and local build outputs
```

Checks used in CI:

```bash
cd daemon && cargo fmt --all -- --check && cargo clippy --all-targets -- -D warnings && cargo test
```

```bash
cd firmware && pio test -e native && pio run -e esp32-c3-supermini
```

Maintainer detail: [`docs/guides/development.md`](docs/guides/development.md).

- CI: [`.github/workflows/ci.yml`](.github/workflows/ci.yml)
- Draft GitHub Releases on `v*` tags (tag must match Cargo version):
  [`.github/workflows/release.yml`](.github/workflows/release.yml)

---

Made with ❤️ by [imflawlezz](https://github.com/imflawlezz)
