#!/usr/bin/env bash
# Flash a released tty-buddy *merged factory* .bin to the ESP32-C3.
# The image must include bootloader + partitions + app (flash at 0x0).
# Do not pass a bare PlatformIO app firmware.bin — that wipes the bootloader.
#
# Usage:
#   ./flash-firmware.sh tty-buddy-firmware.bin
#   ./flash-firmware.sh tty-buddy-firmware.bin --port /dev/ttyACM0
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <merged-factory.bin> [--port /dev/ttyACM0]" >&2
  exit 1
fi

BIN="$1"
shift
PORT=""
while [[ $# -gt 0 ]]; do
  case "$1" in
  --port)
    PORT="${2:-}"
    shift 2
    ;;
  *)
    echo "Unexpected argument: $1" >&2
    exit 1
    ;;
  esac
done

if [[ ! -f "$BIN" ]]; then
  echo "Firmware image not found: $BIN" >&2
  exit 1
fi

# App-only builds are ~300KB at 0x10000; a full merge is larger. Guard the
# common footgun of flashing .pio/.../firmware.bin at 0x0.
size="$(wc -c <"$BIN" | tr -d ' ')"
if [[ "$size" -lt 350000 ]]; then
  echo "Refusing image of $size bytes — looks like an app-only .bin." >&2
  echo "Use a merged factory release image, or from source:" >&2
  echo "  (cd firmware && ./scripts/flash.sh)" >&2
  exit 1
fi

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

if command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL=(esptool.py)
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL=(esptool)
elif [[ -f "$HOME/.platformio/packages/tool-esptoolpy/esptool.py" ]]; then
  ESPTOOL=(python3 "$HOME/.platformio/packages/tool-esptoolpy/esptool.py")
else
  echo "esptool not found. Install with:  pip install esptool" >&2
  exit 1
fi

echo "Flashing $BIN"
echo "  port: $PORT"
if systemctl is-active --quiet tty-buddy.service 2>/dev/null; then
  echo "  stopping tty-buddy.service for flash…"
  sudo systemctl stop tty-buddy.service
  RESTART_DAEMON=1
else
  RESTART_DAEMON=0
fi

"${ESPTOOL[@]}" --chip esp32c3 --port "$PORT" --baud 921600 \
  --before default_reset --after hard_reset \
  write_flash -z 0x0 "$BIN"

if [[ "$RESTART_DAEMON" -eq 1 ]]; then
  echo "  starting tty-buddy.service…"
  sudo systemctl start tty-buddy.service || true
fi

echo "Done. If the panel is blank, unplug/replug the board."
