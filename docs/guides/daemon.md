# Daemon package lifecycle

Covers what the packages install, how install/upgrade behave, and how to
remove them.

## What gets installed

| Path | Purpose |
|------|---------|
| `/usr/bin/tty-buddy` | Daemon binary |
| `/usr/lib/systemd/system/tty-buddy@.service` | Template unit; instance = Linux username |
| `/etc/udev/rules.d/99-tty-buddy.rules` | `303a:1001` → `/dev/tty-buddy`, group `dialout` |
| `/etc/tty-buddy/daemon.toml` | Host settings (created if missing) |
| `/etc/tty-buddy/buddy.config` | Panel config (created if missing) |
| `/usr/lib/tty-buddy/configure-instance.sh` | Shared post-install helper |
| `/usr/lib/tty-buddy/uninstall.sh` | Tarball uninstall only |

The unit runs as `User=%i` / `Group=%i` with supplementary `dialout` and
`input`, working directory `/etc/tty-buddy`, and:

```text
ExecStart=/usr/bin/tty-buddy run --config /etc/tty-buddy/daemon.toml
```

`Restart=always` with a short delay — the process reconnects when the USB
device disappears.

## Instance user

Install must bind to a **non-root** account. That account is:

- the systemd instance (`tty-buddy@alice`)
- `shell_user` in `daemon.toml` (console PTY)
- owner of `buddy.config` (so the user can edit it without root)
- added to `dialout` and `input`

Detection:

- **`.deb` postinst:** `TTY_BUDDY_USER`, else `SUDO_USER`
- **tarball `install.sh`:** same; refuses root-only sudo with no user

Override example:

```bash
sudo TTY_BUDDY_USER=alice ./install.sh
```

`configure-instance.sh` also:

- migrates legacy `status.config` → `buddy.config` when needed
- prepends a default `[behavior]` block if missing
- sets `shell_user` when it is missing or still `REPLACE_ME` / `dih`
- disables a leftover non-template `tty-buddy.service` if present
- `enable --now tty-buddy@<user>`

New `dialout`/`input` membership often requires a new login session before
the daemon can open the keyboard or serial node without permission errors.

## Install / upgrade — `.deb`

Artifacts: `tty-buddy_<version>_amd64.deb` or `_arm64.deb`.

```bash
sudo apt-get install -y ./tty-buddy_<version>_amd64.deb
```

Same command upgrades. dpkg conffiles are `/etc/tty-buddy/daemon.toml` and
`buddy.config`. On upgrade, apt may prompt (or keep old files with
`--force-confold`). Binary, unit, udev, and helper scripts always refresh.

If postinst could not infer a user, it prints how to run
`configure-instance.sh` manually.

## Install / upgrade — tarball

Artifacts: `tty-buddy-<version>-x86_64-linux.tar.gz` or
`tty-buddy-<version>-aarch64-linux.tar.gz`.

```bash
tar xzf tty-buddy-<version>-x86_64-linux.tar.gz
cd tty-buddy-<version>-x86_64-linux
sudo ./install.sh
```

`install.sh` always replaces the binary, unit, udev rule, and helpers. It
**does not** overwrite existing `/etc/tty-buddy/daemon.toml` or
`buddy.config`. First install copies defaults from the tarball’s `etc/`.

Upgrade = unpack the new tarball and run `install.sh` again, then restart if
the unit was already running:

```bash
sudo systemctl restart tty-buddy@$USER
```

## After install checks

```bash
systemctl status tty-buddy@$USER
ls -l /dev/tty-buddy
journalctl -u tty-buddy@$USER -n 50 --no-pager
```

Missing symlink: replug USB, `udevadm trigger`, confirm the rule is
installed. Permission denied on `/dev/input` or serial: fix groups /
re-login.

## Several boards on one host

Default udev symlink is a single `/dev/tty-buddy`. With multiple Espressif
JTAG devices, pin one in `daemon.toml`:

```bash
tty-buddy devices
tty-buddy setup --config /etc/tty-buddy/daemon.toml
sudo systemctl restart tty-buddy@$USER
```

`setup` writes `device_path`, `vid`, `pid`, `serial`, and `shell_user`. Only
one `tty-buddy@user` instance is packaged by default; extra boards need
separate config/unit arrangements (not automated).

## Uninstall

Pick **one** path per packaging style.

### `.deb`

Keep `/etc/tty-buddy`:

```bash
sudo apt-get remove tty-buddy
```

Remove package and its conffiles:

```bash
sudo apt-get purge tty-buddy
```

`prerm` disables `tty-buddy.service` and all `tty-buddy@*` instances.

### Tarball

Keep configs:

```bash
sudo /usr/lib/tty-buddy/uninstall.sh
```

Also delete `/etc/tty-buddy`:

```bash
sudo /usr/lib/tty-buddy/uninstall.sh --purge
```
