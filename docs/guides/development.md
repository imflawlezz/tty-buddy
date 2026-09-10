# Development

How to build, test, package, and release from a checkout. Day-to-day install
and flash steps live in [daemon](daemon.md) and [flashing](flashing.md).
Runtime split: [architecture](../architecture/overview.md).

## Prerequisites

| Piece | Needs |
|-------|--------|
| Firmware | PlatformIO Core (`pio`) |
| Daemon compile / test | Rust stable ([`daemon/rust-toolchain.toml`](../../daemon/rust-toolchain.toml): `rustfmt`, `clippy`), `pkg-config`, `libudev-dev` |
| Packages on this Linux host | `dpkg-deb` / `dpkg-dev` for `.deb` |
| Packages from macOS or foreign arch | Docker (`rust:1-bookworm`, `--platform linux/amd64` or `linux/arm64`) |

The daemon targets **Linux** (udev serial discovery, `/dev/input`, systemd
packaging). You can edit and run unit tests on other hosts where crates
compile; runtime and packages assume Linux amd64/arm64.

**Single version string:** [`daemon/Cargo.toml`](../../daemon/Cargo.toml) `version`. [`Makefile`](../../Makefile),
firmware inject/merge scripts, and CI all read that field. Firmware UI label
is `version/DD.MM.YY` (local build date) via [`inject_version.py`](../../firmware/scripts/inject_version.py).

## Layout

| Path | Role |
|------|------|
| [`firmware/`](../../firmware/) | PlatformIO device + native protocol tests |
| [`daemon/`](../../daemon/) | Rust crate (`tty_buddy` lib + `tty-buddy` bin) |
| [`daemon/packaging/`](../../daemon/packaging/) | `.deb` / tarball builders, unit, install helpers |
| [`daemon/udev/`](../../daemon/udev/) | `99-tty-buddy.rules` |
| [`scripts/`](../../scripts/) | [`write-checksums.sh`](../../scripts/write-checksums.sh), [`changelog-section.sh`](../../scripts/changelog-section.sh) |
| [`dist/`](../../dist/) | Root artifact folder (`make` / release CI) |

Library modules under [`daemon/src/lib.rs`](../../daemon/src/lib.rs): `bridge`, `discover`, `keyboard`,
`metrics`, `protocol`, `serial_io`, `settings`, `status_config`, `terminal`.
Wire format: [protocol](../reference/protocol.md).

## Make targets

From the repository root ([`Makefile`](../../Makefile)). `VERSION` defaults to the Cargo
version; override with `make VERSION=…` only if you know why.

| Target | Result |
|--------|--------|
| `make help` / `make version` | Print targets / version |
| `make firmware` | `pio run` → copy factory bin to `dist/tty-buddy-firmware-<ver>.bin` |
| `make daemon` | Host-arch `.deb` + tarball into `dist/` |
| `make daemon-all` | amd64 **and** arm64 packages (uses Docker when arch ≠ host) |
| `make checksums` | `dist/checksums.txt` for whatever is already in `dist/` |
| `make release` | `firmware` + `daemon` + `checksums` |
| `make release-all` | `firmware` + `daemon-all` + `checksums` |
| `make clean` | Remove `dist/`, `firmware/dist`, `daemon/dist`, PIO/cargo clean |

Packaging scripts also write under `daemon/dist/` and honour `DEST` (Make
sets `DEST=dist`). Artifact names:

```text
tty-buddy-firmware-<ver>.bin
tty-buddy_<ver>_amd64.deb | tty-buddy_<ver>_arm64.deb
tty-buddy-<ver>-x86_64-linux.tar.gz | tty-buddy-<ver>-aarch64-linux.tar.gz
checksums.txt
```

## Firmware pipeline

Env [`esp32-c3-supermini`](../../firmware/platformio.ini) (default): Arduino + TFT_eSPI, USB CDC on boot
(`ARDUINO_USB_MODE` / `ARDUINO_USB_CDC_ON_BOOT`), pins from [`User_Setup.h`](../../firmware/include/User_Setup.h)
and [`osd.cpp`](../../firmware/src/osd.cpp) (`GPIO5` BL, `GPIO10` button). Wiring: [`hardware/wiring.md`](../../hardware/wiring.md).

`extra_scripts` on that env:

1. **pre** [`inject_version.py`](../../firmware/scripts/inject_version.py) — `TTY_BUDDY_VERSION` from Cargo + date.
2. **pre** [`fix_tft_espi_c3.py`](../../firmware/scripts/fix_tft_espi_c3.py) — patches TFT_eSPI’s ESP32-C3 `SPI_PORT`
   (`SPI2_HOST` vs `REG_SPI_BASE` expecting `2`) under `.pio/libdeps/…`.
   Idempotent; skips if the header is not downloaded yet (first resolve may
   need a second build).
3. **post** [`merge_factory.py`](../../firmware/scripts/merge_factory.py) — `esptool merge_bin` of bootloader `@0x0`,
   partitions `@0x8000`, `boot_app0` `@0xe000`, app `@0x10000` →
   `firmware/dist/tty-buddy-firmware-<ver>.bin` (flash at **0x0**). Skipped
   for the `native` env.

`make firmware` fails if that merged file is missing after `pio run`.

Env **`native`**: Unity tests with `test_build_src` filtered to
[`protocol.cpp`](../../firmware/src/protocol.cpp) and
[`glyph_font.cpp`](../../firmware/src/glyph_font.cpp)
([`firmware/test/test_protocol/`](../../firmware/test/test_protocol/),
[`firmware/test/test_glyphs/`](../../firmware/test/test_glyphs/)). Do
**not** run `pio run -e native` — there is no `main()` outside the test
harness.

Unicode cell bitmaps for **Polish and German** diacritics live in
[`glyph_unicode_data.h`](../../firmware/src/glyph_unicode_data.h), generated
via [`scripts/gen_unicode_glyphs.py`](../../firmware/scripts/gen_unicode_glyphs.py)
(font8x8 Public Domain + Unifont SIL OFL 1.1). Other non-ASCII still shows
`?` aside from procedural box/block/Braille.

### Local firmware loop

```bash
cd firmware
pio test -e native
pio run -e esp32-c3-supermini
./scripts/flash.sh          # PIO upload (bootloader + app offsets)
# or flash the merged factory image:
./scripts/flash.sh ../dist/tty-buddy-firmware-$(make -C .. -s version).bin
```

[`flash.sh`](../../firmware/scripts/flash.sh) stops running `tty-buddy@*` and
`tty-buddy-board@*` when systemd is present. Details: [flashing](flashing.md).

Serial monitor (CDC, 115200):

```bash
cd firmware
pio device monitor -e esp32-c3-supermini
```

## Daemon pipeline

Release profile uses LTO + `codegen-units = 1` ([`Cargo.toml`](../../daemon/Cargo.toml)).

### Checks (same as CI)

[`.github/workflows/ci.yml`](../../.github/workflows/ci.yml) on `main` / PRs:

```bash
cd daemon
cargo fmt --all -- --check
cargo clippy --all-targets -- -D warnings
cargo test
cargo build --release   # CI also does this
```

```bash
cd firmware
pio test -e native
pio run -e esp32-c3-supermini
# CI asserts firmware/dist/tty-buddy-firmware-<ver>.bin exists
```

Unit coverage is concentrated in `protocol`, `status_config`, `metrics`,
`settings`, `discover`, `keyboard`, `terminal` (`#[cfg(test)]` in those
files). There is no hardware-in-the-loop job in CI.

### Run without packaging

```bash
cd daemon
cargo run --release --bin tty-buddy -- devices
cargo run --release --bin tty-buddy -- probe --buddy-config ./buddy.config
cargo run --release --bin tty-buddy -- run --config /path/to/daemon.toml
```

`--port`, `--buddy-config`, `--fps`, `--status` / `--terminal` override
config for that process only ([configuration](../reference/configuration.md)). Prefer `tty-buddy@$USER` on a
real host so udev groups and restart policy match production ([daemon](daemon.md)).

## Packaging internals

Entry points:

- [`build-deb.sh`](../../daemon/packaging/build-deb.sh)
- [`build-linux-release.sh`](../../daemon/packaging/build-linux-release.sh)
- shared arch resolution: [`arch.sh`](../../daemon/packaging/arch.sh)
- binary: [`build-binary.sh`](../../daemon/packaging/build-binary.sh)

[`build-binary.sh`](../../daemon/packaging/build-binary.sh) runs `cargo build --release` **natively** only when the
host is Linux **and** matches the requested deb arch. Otherwise it builds
inside Docker (`--platform linux/amd64` or `linux/arm64`, `rust:1-bookworm`
+ `libudev-dev`). So:

- Linux amd64 → native amd64 packages; arm64 packages need Docker (or an ARM
  runner).
- macOS → always Docker for both arches (and for `dpkg-deb` if missing —
  `build-deb.sh` re-enters itself in the container).

`.deb` ships binary, `tty-buddy@` / `tty-buddy-board@` units, udev,
`configure-instance.sh`, and default `daemon.toml` / `buddy.config`. Tarball
adds `install.sh` / `uninstall.sh`. Behaviour after install: [daemon](daemon.md).

## Release workflow

[`.github/workflows/release.yml`](../../.github/workflows/release.yml):

| Trigger | Behaviour |
|---------|-----------|
| Push tag `v*` | Build all artifacts; tag must equal Cargo version (`v1.0.0` ↔ `1.0.0`); **draft** GitHub Release |
| `workflow_dispatch` | Always builds artifacts; creates/updates a draft release only if `tag` input is set |

Jobs: firmware (`make firmware`), daemon matrix (amd64 on `ubuntu-latest`,
arm64 on `ubuntu-24.04-arm`), then aggregate + [`write-checksums.sh`](../../scripts/write-checksums.sh). Release
notes body = [`changelog-section.sh`](../../scripts/changelog-section.sh) for that version from [`CHANGELOG.md`](../../CHANGELOG.md)
(Keep a Changelog section must be non-empty).

### Checklist before tagging

1. Bump `version` in [`daemon/Cargo.toml`](../../daemon/Cargo.toml).
2. Add `## [<ver>] - YYYY-MM-DD` under Keep a Changelog in [`CHANGELOG.md`](../../CHANGELOG.md)
   (not only `[Unreleased]`).
3. Align root README version line if you treat it as released (owned by
   `/readme`).
4. Local smoke (optional): `make release` or `make release-all`, flash
   factory bin, install package on a Linux host.
5. `git tag v<ver> && git push origin v<ver>`.
6. Publish the draft release on GitHub after checking assets and notes.

Local preview of notes:

```bash
./scripts/changelog-section.sh 1.0.0 CHANGELOG.md
```
