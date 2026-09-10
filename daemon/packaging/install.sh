#!/usr/bin/env bash
# Install from an unpacked release tarball (run as root).
# Configures tty-buddy@$SUDO_USER (override with TTY_BUDDY_USER).
set -euo pipefail

if [[ "${EUID:-}" -ne 0 ]]; then
  echo "Run as root:  sudo $0" >&2
  exit 1
fi

HERE="$(cd "$(dirname "$0")" && pwd)"

USER_NAME="${TTY_BUDDY_USER:-${SUDO_USER:-}}"
if [[ -z "$USER_NAME" || "$USER_NAME" == "root" ]]; then
  echo "Could not detect a non-root install user." >&2
  echo "Re-run as:  sudo ./install.sh" >&2
  echo "Or set:     sudo TTY_BUDDY_USER=<you> ./install.sh" >&2
  exit 1
fi
if ! id "$USER_NAME" >/dev/null 2>&1; then
  echo "User '$USER_NAME' does not exist on this system." >&2
  exit 1
fi

install -d /usr/bin /etc/tty-buddy /usr/lib/systemd/system /etc/udev/rules.d /usr/lib/tty-buddy
install -m 755 "$HERE/bin/tty-buddy" /usr/bin/tty-buddy
install -m 644 "$HERE/systemd/tty-buddy@.service" /usr/lib/systemd/system/tty-buddy@.service
install -m 644 "$HERE/systemd/tty-buddy-board@.service" /usr/lib/systemd/system/tty-buddy-board@.service
install -m 644 "$HERE/udev/99-tty-buddy.rules" /etc/udev/rules.d/99-tty-buddy.rules
install -m 755 "$HERE/lib/configure-instance.sh" /usr/lib/tty-buddy/configure-instance.sh
install -m 755 "$HERE/uninstall.sh" /usr/lib/tty-buddy/uninstall.sh

if [[ ! -f /etc/tty-buddy/buddy.config ]]; then
  if [[ -f /etc/tty-buddy/status.config ]]; then
    mv /etc/tty-buddy/status.config /etc/tty-buddy/buddy.config
  elif [[ -f "$HERE/etc/buddy.config" ]]; then
    install -m 644 "$HERE/etc/buddy.config" /etc/tty-buddy/buddy.config
  elif [[ -f "$HERE/etc/status.config" ]]; then
    install -m 644 "$HERE/etc/status.config" /etc/tty-buddy/buddy.config
  fi
fi
if [[ ! -f /etc/tty-buddy/daemon.toml ]]; then
  install -m 644 "$HERE/etc/daemon.toml" /etc/tty-buddy/daemon.toml
fi

# Remove pre-template unit left by older tarball installs.
rm -f /etc/systemd/system/tty-buddy.service

"$HERE/lib/configure-instance.sh" "$USER_NAME"

echo
echo "Installed tty-buddy for user '$USER_NAME'."
echo "  binary : /usr/bin/tty-buddy"
echo "  config : /etc/tty-buddy/"
echo "  service: systemctl status tty-buddy@${USER_NAME}"
echo
echo "Next:"
echo "  1. Plug in the ESP (or unplug/replug) so udev creates /dev/tty-buddy"
echo "  2. If dialout/input are new for this session, log out/in once, then:"
echo "       sudo systemctl restart tty-buddy@${USER_NAME}"
echo
echo "Optional (several boards):  sudo /usr/lib/tty-buddy/configure-instance.sh add-board $USER_NAME <serial>"
echo "Uninstall later:            sudo /usr/lib/tty-buddy/uninstall.sh [--purge]"
