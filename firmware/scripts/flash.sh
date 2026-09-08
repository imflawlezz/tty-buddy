#!/usr/bin/env bash
# Flash tty-buddy firmware to the first Espressif ESP32-C3 USB-JTAG board.
#
# Usage:
#   ./flash.sh                         # PlatformIO upload (bootloader + app)
#   ./flash.sh --port /dev/ttyACM0
#   ./flash.sh path/to/merged-factory.bin   # full image at 0x0 (release artifact)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
FW_ROOT="$(cd "$HERE/.." && pwd)"
BUILD_DIR="$FW_ROOT/.pio/build/esp32-c3-supermini"
ENV_NAME="esp32-c3-supermini"

BIN=""
PORT=""

while [[ $# -gt 0 ]]; do
  case "$1" in
  --port)
    PORT="${2:-}"
    shift 2
    ;;
  -h | --help)
    sed -n '2,8p' "$0" | sed 's/^# \?//'
    exit 0
    ;;
  *)
    if [[ -n "$BIN" ]]; then
      echo "Unexpected argument: $1" >&2
      exit 1
    fi
    BIN="$1"
    shift
    ;;
  esac
done

find_port() {
  if [[ -e /dev/tty-buddy ]]; then
    echo /dev/tty-buddy
    return 0
  fi
  local node props
  for node in /dev/ttyACM* /dev/ttyUSB*; do
    [[ -e "$node" ]] || continue
    props="$(udevadm info -q property -n "$node" 2>/dev/null || true)"
    if echo "$props" | grep -qiE '^ID_VENDOR_ID=303a$' \
      && echo "$props" | grep -qiE '^ID_MODEL_ID=1001$'; then
      echo "$node"
      return 0
    fi
  done
  return 1
}

if [[ -z "$PORT" ]]; then
  if ! PORT="$(find_port)"; then
    echo "No ESP32-C3 USB-JTAG serial port found (303a:1001)." >&2
    echo "Plug the board in over USB, then retry." >&2
    exit 1
  fi
fi

# Stop host daemon so it does not hold the serial port during flash.
if systemctl is-active --quiet tty-buddy.service 2>/dev/null; then
  echo "  stopping tty-buddy.service for flash…"
  sudo systemctl stop tty-buddy.service
  RESTART_DAEMON=1
else
  RESTART_DAEMON=0
fi

resolve_esptool() {
  if command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL=(esptool.py)
    return 0
  fi
  if command -v esptool >/dev/null 2>&1; then
    ESPTOOL=(esptool)
    return 0
  fi
  local pio_esptool="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
  if [[ -f "$pio_esptool" ]]; then
    ESPTOOL=(python3 "$pio_esptool")
    return 0
  fi
  echo "esptool not found. Install with:  pip install esptool" >&2
  echo "Or install PlatformIO (ships esptool)." >&2
  return 1
}

if [[ -z "$BIN" ]]; then
  # Preferred path: let PlatformIO write bootloader + partitions + app.
  if command -v pio >/dev/null 2>&1; then
    echo "Flashing via PlatformIO ($ENV_NAME)"
    echo "  port: $PORT"
    (cd "$FW_ROOT" && pio run -e "$ENV_NAME" -t upload --upload-port "$PORT")
  else
    resolve_esptool
    for f in bootloader.bin partitions.bin firmware.bin; do
      if [[ ! -f "$BUILD_DIR/$f" ]]; then
        echo "Missing $BUILD_DIR/$f — build first: (cd firmware && pio run -e $ENV_NAME)" >&2
        exit 1
      fi
    done
    BOOT_APP0=""
    for cand in \
      "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
      /usr/share/arduino/hardware/espressif/esp32/tools/partitions/boot_app0.bin; do
      if [[ -f "$cand" ]]; then
        BOOT_APP0="$cand"
        break
      fi
    done
    if [[ -z "$BOOT_APP0" ]]; then
      echo "boot_app0.bin not found (install PlatformIO or pass a merged factory .bin)." >&2
      exit 1
    fi
    echo "Flashing bootloader + partitions + app"
    echo "  port: $PORT"
    "${ESPTOOL[@]}" --chip esp32c3 --port "$PORT" --baud 921600 \
      --before default_reset --after hard_reset \
      write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB \
      0x0000 "$BUILD_DIR/bootloader.bin" \
      0x8000 "$BUILD_DIR/partitions.bin" \
      0xe000 "$BOOT_APP0" \
      0x10000 "$BUILD_DIR/firmware.bin"
  fi
else
  resolve_esptool
  if [[ ! -f "$BIN" ]]; then
    echo "Firmware image not found: $BIN" >&2
    exit 1
  fi
  # Release / factory images are full-chip merges flashed at 0x0.
  # Never flash a bare PlatformIO app firmware.bin at 0x0 — that wipes the bootloader.
  size="$(wc -c <"$BIN" | tr -d ' ')"
  if [[ "$size" -lt 200000 ]]; then
    echo "Refusing to flash a small image ($size bytes) at 0x0." >&2
    echo "That looks like an app-only .bin. Use:  ./flash.sh   (no args, via PlatformIO)" >&2
    echo "Or pass a merged factory image from a release." >&2
    exit 1
  fi
  echo "Flashing merged image $BIN"
  echo "  port: $PORT"
  "${ESPTOOL[@]}" --chip esp32c3 --port "$PORT" --baud 921600 \
    --before default_reset --after hard_reset \
    write_flash -z 0x0 "$BIN"
fi

if [[ "$RESTART_DAEMON" -eq 1 ]]; then
  echo "  starting tty-buddy.service…"
  sudo systemctl start tty-buddy.service || true
fi

echo "Done. Unplug/replug if the display stays blank, then check: systemctl status tty-buddy"
