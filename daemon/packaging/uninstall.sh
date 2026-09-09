#!/usr/bin/env bash
# Uninstall a tarball install (run as root).
# Default keeps /etc/tty-buddy; --purge deletes configs too.
set -euo pipefail

if [[ "${EUID:-}" -ne 0 ]]; then
  echo "Run as root:  sudo $0 [--purge]" >&2
  exit 1
fi

PURGE=0
for arg in "$@"; do
  case "$arg" in
  --purge) PURGE=1 ;;
  -h | --help)
    echo "Usage: sudo $0 [--purge]"
    exit 0
    ;;
  *)
    echo "Unexpected argument: $arg" >&2
    exit 1
    ;;
  esac
done

if command -v systemctl >/dev/null 2>&1; then
  systemctl disable --now tty-buddy.service 2>/dev/null || true
  while read -r unit; do
    [[ -n "$unit" ]] || continue
    systemctl disable --now "$unit" 2>/dev/null || true
  done < <(systemctl list-units --type=service --all --plain --no-legend 'tty-buddy@*' 2>/dev/null | awk '{print $1}')
fi

rm -f /usr/bin/tty-buddy
rm -f /usr/lib/systemd/system/tty-buddy@.service
rm -f /etc/systemd/system/tty-buddy.service
rm -f /etc/udev/rules.d/99-tty-buddy.rules
rm -f /usr/lib/tty-buddy/configure-instance.sh
rm -f /usr/lib/tty-buddy/uninstall.sh
rmdir /usr/lib/tty-buddy 2>/dev/null || true

if [[ "$PURGE" -eq 1 ]]; then
  rm -rf /etc/tty-buddy
  echo "Removed configs under /etc/tty-buddy."
else
  echo "Left /etc/tty-buddy in place (use --purge to delete configs)."
fi

if command -v systemctl >/dev/null 2>&1; then
  systemctl daemon-reload || true
fi
if command -v udevadm >/dev/null 2>&1; then
  udevadm control --reload-rules || true
  udevadm trigger || true
fi

echo "Uninstalled tty-buddy."
