# Configuration reference

Two files after a normal install:

| File | Role | Edited by | Applied |
|------|------|-----------|---------|
| `daemon.toml` | Host bind: serial device, `shell_user`, path to panel file | packaging, `tty-buddy setup`, admin | **Process restart** |
| `buddy.config` | Panel UX: layout colours, behaviour, alerts, display/OSD | user, OSD writeback | **mtime poll** in the running session |

Shipped templates:

- [`daemon/packaging/daemon.toml.example`](../../daemon/packaging/daemon.toml.example)
- [`daemon/buddy.config`](../../daemon/buddy.config)

Installed defaults: `/etc/tty-buddy/daemon.toml`,
`/etc/tty-buddy/buddy.config`.

Implementation: [`daemon/src/settings.rs`](../../daemon/src/settings.rs), [`daemon/src/status_config.rs`](../../daemon/src/status_config.rs),
[`daemon/src/bridge.rs`](../../daemon/src/bridge.rs). Wire fields: [protocol](protocol.md).

---

## Reload matrix

| Change | Effect |
|--------|--------|
| Any `daemon.toml` key | Restart `tty-buddy@…` (or the process) |
| `buddy.config` mtime | Re-parse; push `FLAG_STYLE`; update `fps`. Keyboard-watch policy is reapplied only while already in **status** mode |
| `startup_mode` while running | Stored in memory only — **does not** flip status ↔ terminal mid-session |
| CLI `--status` / `--terminal` | Applied once when the session starts; not re-applied on buddy reload |
| CLI `--fps` | Applied at session start and again on every buddy reload |
| Device USB reconnect | New session: reloads buddy.config; same in-memory `daemon.toml` + CLI overrides |
| OSD brightness / sleep on device | Rewrites `brightness` + `sleep_timeout` in buddy.config (see [OSD writeback](#osd-writeback)) |

Status snapshots stay ~1 Hz for `/proc` metrics (CPU, mem, disk, load, …).
Interface IPs (`ip`) and systemd service lists (`systemctl`) are cached for
about **3 seconds** between forks. `fps` caps **terminal** frame sends only.

---

## `daemon.toml`

TOML. Search order when `--config` is omitted ([`settings_path`](../../daemon/src/settings.rs)):

1. `./daemon.toml` if it exists
2. `/etc/tty-buddy/daemon.toml` if it exists
3. `$XDG_CONFIG_HOME/tty-buddy/daemon.toml` (via `dirs::config_dir`)
4. else `/etc/tty-buddy/daemon.toml`

Missing file → `DaemonSettings::default()`. Invalid or unreadable
`daemon.toml` aborts startup / setup instead of silently falling back.

### Host keys

| Key | Type | Default (`Default` impl) | Meaning |
|-----|------|--------------------------|---------|
| `device_path` | string? | unset | Preferred node (usually `/dev/tty-buddy`) |
| `vid` | u16? | `0x303A` (`12346`) | USB vendor |
| `pid` | u16? | `0x1001` (`4097`) | USB product (Espressif USB-Serial/JTAG) |
| `serial` | string? | unset | Disambiguate when several boards share vid/pid |
| `buddy_config` | path? | `/etc/tty-buddy/buddy.config` | Panel INI. **Alias:** `status_config` |
| `shell_user` | string? | unset | Login shell identity inside the PTY; must match the daemon runtime user / systemd instance |

If a **present** file omits `buddy_config`, serde yields `None` and the
[panel path resolver](#panel-file-resolution) searches the usual candidates (unlike `Default`, which
sets `/etc/tty-buddy/buddy.config`).

### Discovery (`pick_device`)

1. `device_path` if that path exists
2. Else match `vid`/`pid` (and `serial` if set)
3. Else first Espressif `303a:1001`
4. Else sole listed tty / `/dev/tty-buddy` / first candidate

`tty-buddy devices` lists candidates. `tty-buddy setup` writes host fields
only (see [CLI](#cli)).

For stable multi-board setups, set `serial` as well as `device_path`/USB ids.

### Legacy behaviour keys (host file)

Still deserialized:

| Key | Default | Notes |
|-----|---------|-------|
| `start_in_status` | `true` | |
| `keyboard_opens_terminal` | `true` | |
| `fps` | `10.0` | |

They apply **only** when buddy.config has **no** `[behavior]` section
(`apply_daemon_behavior_fallback`). `DaemonSettings::save` / `setup`
**omit** them — behaviour belongs in buddy.config.

---

## `buddy.config`

INI. Section and key names are trimmed and **ASCII-lowercased**. Blank lines
and full-line `#` / `;` comments are skipped. Inline comments (`#` / `;`)
count only when preceded by whitespace. Keys before the first `[section]`
are ignored. Missing file → built-in defaults (no error).

### Panel file resolution

[`resolve_buddy_config_path`](../../daemon/src/settings.rs) — first **existing file** among:

1. Path from `daemon.toml` / `--buddy-config`
2. `./buddy.config`
3. `/etc/tty-buddy/buddy.config`
4. `./status.config`
5. `/etc/tty-buddy/status.config`

If none exist → configured path, else `/etc/tty-buddy/buddy.config`.

### Shared value types

**Bool** (`as_bool`): `1` / `true` / `yes` / `on` → true; `0` / `false` /
`no` / `off` → false; empty or anything else → the call’s default.

**Colour** (`parse_hex_color`): optional `#`; 3-digit hex expands (`#F00` →
`FF0000`); else 6 hex digits; bare `888888` accepted. Invalid → caller
default. Stored as RGB565 on the wire.

**Date / time formats**: C-style `%` tokens mapped on the host (
[`strftime_chrono`](../../daemon/src/metrics.rs)). Supported: `Y y m d H I M S b B a A p %`. Unknown `%X`
is left as the literal `%X`. Not full libc strftime.

---

### `[behavior]`

Presence sets `behavior_from_file` and disables daemon.toml legacy behaviour
keys.

| Key | Alias | Default | Semantics |
|-----|-------|---------|-------------|
| `startup_mode` | `start_in_status` | status | Terminal if value is `terminal`, `console`, `tty`, `false`, `0`, `no`, or `off` (case-insensitive). **Any other string → status** |
| `keyboard_opens_terminal` | — | `true` | In status mode, watch matching host keyboards without grab; any activity opens terminal and sets activity/wake |
| `fps` | — | `10` | Terminal frame cap; parsed value `.max(1.0)` |

`keyboard_opens_terminal = true` is convenient on a dedicated host console,
but it is aggressive on a desktop: typing on a watched keyboard can pull the
panel into terminal mode. Set it to `false` to disable status-mode keyboard
watch entirely.

Packaging may prepend a default `[behavior]` block if missing
([daemon lifecycle](../guides/daemon.md)).

---

### `[globals]`

| Key | Alias | Default |
|-----|-------|---------|
| `label_color` | — | `#888888` |
| `background_color` | `background` | `#000000` |

---

### `[header]`

| Key | Default |
|-----|---------|
| `hostname_color` | white |
| `date_color` | `#888888` |
| `time_color` | white |
| `date_format` | `%d-%m-%Y` (applied only if non-empty) |
| `time_format` | `%H:%M:%S` (applied only if non-empty) |

---

### `[hero]`

CPU / MEM / DISK meters and thresholds.

| Key | Aliases | Default / notes |
|-----|---------|-----------------|
| `cpu_color` | `cpu` | white if empty |
| `mem_color` | `mem` | white if empty |
| `disk_color` | `disk` | white if empty |
| `disk_mount` | — | `/` (non-empty only) |
| `meter_mode` | — | bool; **if the section is present and the key is omitted, treated as `true`** |
| `warn_at` | — | `60` (`u8`; parse fail → 60; no clamp). If `warn_at > crit_at` after parse, the two values are **swapped** |
| `crit_at` | — | `90` (same swap rule as `warn_at`) |
| `level_ok_color` | `level_ok` | `#33AA33` |
| `level_warn_color` | `level_warn` | `#CCCC33` |
| `level_crit_color` | `level_crit` | `#CC3333` |

---

### `[secondary]`

Two optional fields under the hero row.

| Key | Values |
|-----|--------|
| `left` / `right` | `uptime` / `up` → uptime; `load` → load average; `swap` → swap; `""` / `none` / `blank` / unknown → hidden |
| `left_color` / `right_color` | empty → white |

Code defaults both sides to hidden (`SEC_NONE`). The shipped template sets
`left = uptime`, `right = load`.

---

### `[interfaces]`

Each key is an interface name (stored lowercased); value is the address
mode. **File order is preserved.** At most **16** interfaces are sampled
onto the wire.

| Value | Mode |
|-------|------|
| `v6`, `6`, `ipv6` | IPv6 |
| `auto`, `any`, `both` | Prefer IPv4; else non-`fe80:` IPv6; else link-local |
| anything else (incl. `v4`, `4`, `ipv4`, garbage) | IPv4 |

Empty section / no keys → no interface rows. The shipped template leaves this
section empty on purpose; add only the interfaces you want shown. Address
lookups use `ip` and share the ~3 s slow-poll cache with services (see
[Reload matrix](#reload-matrix)).

---

### `[services]`

| Key | Aliases | Notes |
|-----|---------|-------|
| `filter` | — | Missing → `"all"`. `""` / `all` / `*` → unfiltered. Else comma-separated unit names; `.service` appended if absent; names ending in `@` skipped |
| `active_color` | `active` | |
| `failed_color` | `failed` | |
| `inactive_color` | `inactive` | |
| `activating_color` | `activating` | |
| `reloading_color` | `reloading` | |
| `deactivating_color` | `deactivating` | |
| `maintenance_color` | `maintenance` | |

Unfiltered: `systemctl list-units --type=service --all …`, then sort failed
→ transitioning → active → inactive → other, truncate to **80**. Display
names strip `.service` and truncate to **40** characters. Fresh `systemctl`
forks are rate-limited by the ~3 s slow-poll cache (see
[Reload matrix](#reload-matrix)).

---

### `[alerts]`

If the section is **absent**, `alert_mask` stays `0` (alerts off) — even
though code defaults for colours/temp still exist. The shipped template
includes `[alerts]` with sources enabled.

If the section is **present**, bits are OR’d from the per-source bools, then
`enabled = false` clears the whole mask.

| Key | Default if missing | Bit |
|-----|--------------------|-----|
| `cpu_crit` | `true` | CPU over `crit_at` |
| `mem_crit` | `true` | MEM over `crit_at` |
| `disk_crit` | `true` | DISK over `crit_at` |
| `temp_crit` | `true` | temperature ≥ `temp_crit_c` |
| `service_failed` | `true` | failed units |
| `service_inactive` | **`false`** | inactive units (noisy; off by default) |
| `enabled` | — | if present and false → mask `0` |
| `background_color` | `#990000` | |
| `text_color` | white | |
| `hold_sec` | `0` | `0` / `""` / `until_clear` / `until-clear` / `clear` → hold until condition clears; else seconds (`u8`) |
| `temp_crit_c` | `80` | parse fail → 80 |

---

### `[display]` (legacy `[osd]`)

Read as `[display]`, else `[osd]`. If both exist, **`[display]` wins**. OSD
writeback always rewrites the header as `[display]`.

When this section is present, OSD flags are **rebuilt from zero** (not
merged with the previous default bitset).

| Key | Aliases | Semantics |
|-----|---------|-------------|
| `brightness` | `default_brightness` | `auto` → auto mode (level `0` + auto flag). Else `u8` clamped with `.min(100)` on parse |
| `auto_brightness` | — | bool (default false); also set when `brightness = auto`. Writeback **drops** this key in favour of `brightness = auto` |
| `sleep_timeout` | `sleep_timeout_sec` | `never` / `""` → never (`0`). Else integer (see levels below) |
| `auto_day_level` | `auto_day_pct` | `.min(100)`; firmware maps 1–6 as steps, `>6` as approximate % |
| `auto_night_level` | `auto_night_pct` | same |
| `auto_day_hour` / `auto_night_hour` | — | local hour 0–23 (`.min(23)`) |
| `dismiss_alert_on_tap` | — | default `true` — short press dismisses alert instead of only opening OSD |
| `wake_on_alert` | — | default `true` — see below |

When `wake_on_alert` is true:

- an active status alert wakes the panel if it was asleep
- while the alert is up, the sleep timeout does **not** arm
- when the alert clears, the sleep timer restarts from that moment
- OSD idle auto-close is separate and still runs during an alert

When false, alerts do not wake sleep or postpone it.

#### Brightness and sleep levels

Intended form (what OSD writeback persists): **steps `1`–`6`**, plus `auto`
/ `never`.

Firmware ([`osd.cpp`](../../firmware/src/osd.cpp)):

| Brightness step | PWM duty | ~% |
|-----------------|----------|-----|
| 1 | 42 | 16 |
| 2 | 85 | 33 |
| 3 | 128 | 50 |
| 4 | 170 | 67 |
| 5 | 213 | 84 |
| 6 | 255 | 100 |

| Sleep value | Meaning |
|-------------|---------|
| `0` / `never` | Never sleep |
| `1` | 30 s |
| `2` | 60 s |
| `3` | 2 min |
| `4` | 5 min |
| `5` | 10 min |
| `6` | 30 min |

Host → device:

- Brightness `1`–`6` → step; `>6` → treat as percent and map onto six steps;
  `0` + auto flag → auto.
- Sleep `0`–`6` → level index; **`>6` treated as seconds** and mapped onto
  the nearest `SLEEP_SECS` entry.

So `sleep_timeout = 4` in the shipped file is **level 4 (5 minutes)**, not 4
seconds. Prefer levels (or `never`) in hand-edited files; raw seconds also
work if `>6`.

---

## OSD writeback

Device → host events (`DEV_OSD_BRIGHT` / `DEV_OSD_SLEEP`): levels `0`–`6`
(`0` = auto / never).

The bridge updates in-memory style and calls `write_osd_levels` on the
active buddy.config path:

- Rewrites only `brightness` and `sleep_timeout`
- Renames `[osd]` → `[display]`
- Replaces legacy `default_brightness` / `sleep_timeout_sec`
- Drops `auto_brightness` lines
- Updates recorded mtime so the next poll does not thrash-reload
- Does **not** push a style frame afterward (device already applied locally)

Other `[display]` keys are left alone.

---

## CLI

```text
tty-buddy run   [--config PATH] [-p|--port PATH]
                [--buddy-config PATH] [--fps N] [--status | --terminal]
tty-buddy setup [--config PATH]
tty-buddy devices
tty-buddy probe [--buddy-config PATH]
```

| Flag / command | Effect |
|----------------|--------|
| `--config` | `daemon.toml` path (skips search) |
| `-p` / `--port` | Force `device_path` for this process |
| `--buddy-config` | Panel file (`--status-config` alias) |
| `--fps` | Override fps (`.max(1.0)`); reapplied on buddy reload |
| `--status` | Force status mode at session start |
| `--terminal` | Force terminal mode; if both flags set, **`--terminal` wins** (applied last) |
| `setup` | Interactive device pick; `save` host-only fields |
| `devices` | List serial candidates |
| `probe` | Load panel config, sample metrics twice, print summary — **no serial open** |

Systemd: `ExecStart=/usr/bin/tty-buddy run --config /etc/tty-buddy/daemon.toml`.

`shell_user` is not a uid switch. The daemon already runs as `tty-buddy@<user>`
and spawns that same user’s login shell (`$SHELL -l`, home/env from
`/etc/passwd`) inside the PTY. A mismatched `shell_user` causes startup to
fail.

---

## Migration notes

| Old | Current |
|-----|---------|
| `/etc/tty-buddy/status.config` | Still resolved if `buddy.config` is absent; packaging may rename |
| TOML `status_config` | Alias of `buddy_config` |
| `[osd]` | Still read; writeback normalizes to `[display]` |
| Behaviour in `daemon.toml` | Legacy fallback only without `[behavior]` |
| `default_brightness` / `sleep_timeout_sec` / `auto_brightness` | Still read; writeback canonicalizes |

---

## Minimal examples

Host (after install / `setup`):

```toml
device_path = "/dev/tty-buddy"
vid = 12346
pid = 4097
buddy_config = "/etc/tty-buddy/buddy.config"
shell_user = "alice"
```

Panel behaviour + display (fragment):

```ini
[behavior]
startup_mode = status
keyboard_opens_terminal = true
fps = 10

[display]
brightness = 4
sleep_timeout = 4
wake_on_alert = true
dismiss_alert_on_tap = true
```

Full colour / interface / service layout: copy [`daemon/buddy.config`](../../daemon/buddy.config) and
edit.
