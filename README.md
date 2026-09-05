# tty-buddy

ESP32-C3 + ST7789 terminal buddy, with a host daemon and hardware files.

This is a **demo** to evaluate the concept and hardware stack. A Rust daemon migration is under development.

| Path | Contents |
|------|----------|
| [`firmware/`](firmware/) | ESP32 firmware (PlatformIO / Arduino) |
| [`daemon/`](daemon/) | Host daemon |
| [`hardware/`](hardware/) | Wiring notes + CAD / enclosure |

## Quick start (firmware)

```bash
cd firmware
pio run -t upload
```

## Quick start (bridge)

```bash
python3 daemon/tools/tty_bridge.py -p /dev/cu.usbmodem14401
```
