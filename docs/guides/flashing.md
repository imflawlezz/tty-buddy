# Flashing firmware

## Which file to flash

Releases and `make firmware` produce a **merged factory** image:

`tty-buddy-firmware-<version>.bin`

That blob is bootloader + partition table + `boot_app0` + application, laid
out for a write starting at **flash offset 0x0**. Size is on the order of
hundreds of KB (far larger than an app-only binary).

The PlatformIO build also emits
`firmware/.pio/build/esp32-c3-supermini/firmware.bin`. That file is
**application only** and belongs at **0x10000** together with separate
bootloader/partition images. Flashing it alone at `0x0` bricks the layout.
The repo `flash.sh` refuses merged-at-0x0 flashes for images that look too
small.

Version string inside the image comes from `daemon/Cargo.toml` via
`firmware/scripts/inject_version.py` (label also includes a build date).

## Serial port ownership

The host daemon opens the CDC port exclusively. Web flashers and esptool
need it free:

```bash
sudo systemctl stop 'tty-buddy@*' 'tty-buddy-board@*'
```

`firmware/scripts/flash.sh` stops running `tty-buddy.service`,
`tty-buddy@*`, and `tty-buddy-board@*` units when systemd is present, then
restarts the ones it stopped after a successful flash.

Port discovery in `flash.sh`:

1. `/dev/tty-buddy` (udev symlink for `303a:1001`)
2. Otherwise first `/dev/ttyACM*` / `/dev/ttyUSB*` with vendor `303a` and
   product `1001`

Override with `--port`.

## Browser (Web Serial)

Requires the **Web Serial** API: **Chrome** or **Edge 89+**. Not Firefox or
Safari. Origin must be secure (HTTPS); the public tools already are.

| Tool | URL |
|------|-----|
| Espressif esptool-js | https://espressif.github.io/esptool-js/ |
| Adafruit WebSerial ESPTool | https://adafruit.github.io/Adafruit_WebSerial_ESPTool/ |

Procedure:

1. Stop the daemon (above).
2. Plug the board in over USB.
3. In the tool, connect the Espressif USB Serial/JTAG interface.
4. Select `tty-buddy-firmware-<version>.bin`.
5. Program at address **0x0** (full chip image — not an app offset like
   `0x10000`).
6. Wait for verify / reset. If the panel stays black, unplug/replug once.

Failures are usually another process holding the port, a bad cable/hub, or a
Chrome Web Serial regression (update the browser). Android Chrome may work
via WebUSB polyfills in some tools; desktop Chrome/Edge is the supported
path.

## Command line on a machine with the board attached

### Factory image (release / `make firmware`)

```bash
cd firmware
./scripts/flash.sh /path/to/tty-buddy-firmware-<version>.bin
```

```bash
./scripts/flash.sh --port /dev/ttyACM0 /path/to/tty-buddy-firmware-<version>.bin
```

Uses esptool (`esptool.py`, `esptool`, or PlatformIO’s bundled copy) roughly
as:

```bash
esptool.py --chip esp32c3 --port <PORT> --baud 921600 \
  --before default_reset --after hard_reset \
  write_flash -z 0x0 tty-buddy-firmware-<version>.bin
```

### PlatformIO incremental upload (dev)

No path argument uploads bootloader, partitions, `boot_app0`, and app at the
offsets PlatformIO expects (not a single `0x0` merge):

```bash
cd firmware
./scripts/flash.sh
# equivalent: pio run -e esp32-c3-supermini -t upload --upload-port <PORT>
```

Prefer the factory `.bin` when installing from a release so the on-device
partition set matches what CI built.

## After flashing

- USB may renumerate; wait for `/dev/tty-buddy` or the ACM node to reappear.
- Start the daemon again if `flash.sh` did not (or if you stopped it by
  hand):

```bash
sudo systemctl start tty-buddy@$USER
# if you use board instances:
# sudo systemctl start 'tty-buddy-board@*'
```
