#!/usr/bin/env bash
# Install from a release tree. Run as root: sudo ./install.sh
#
# Zero-config path for a new machine:
#   1) flash firmware  2) sudo ./install.sh  3) plug in the ESP
set -euo pipefail

if [[ "${EUID:-}" -ne 0 ]]; then
  echo "Run as root:  sudo $0" >&2
  exit 1
fi

HERE="$(cd "$(dirname "$0")" && pwd)"

# Login user who invoked sudo (not root). Override: sudo TTY_BUDDY_USER=alice ./install.sh
USER_NAME="${TTY_BUDDY_USER:-${SUDO_USER:-}}"
if [[ -z "$USER_NAME" || "$USER_NAME" == "root" ]]; then
  echo "Could not detect a non-root install user." >&2
  echo "Re-run as:  sudo -u <you> sudo ./install.sh" >&2
  echo "Or set:     sudo TTY_BUDDY_USER=<you> ./install.sh" >&2
  exit 1
fi
if ! id "$USER_NAME" >/dev/null 2>&1; then
  echo "User '$USER_NAME' does not exist on this system." >&2
  exit 1
fi

install -d /usr/local/bin /etc/tty-buddy /etc/systemd/system /etc/udev/rules.d
install -m 755 "$HERE/bin/tty-buddy" /usr/local/bin/tty-buddy

if [[ ! -f /etc/tty-buddy/status.config ]]; then
  install -m 644 -o "$USER_NAME" -g "$USER_NAME" "$HERE/etc/status.config" /etc/tty-buddy/status.config
else
  # Daemon runs as USER_NAME and must be able to write OSD brightness/sleep back.
  chown "$USER_NAME:$USER_NAME" /etc/tty-buddy/status.config
  chmod 644 /etc/tty-buddy/status.config
fi

if [[ ! -f /etc/tty-buddy/daemon.toml ]]; then
  install -m 644 -o root -g root "$HERE/etc/daemon.toml" /etc/tty-buddy/daemon.toml
fi
chown root:root /etc/tty-buddy
# Keep the directory traversable; status.config owned by service user.
chmod 755 /etc/tty-buddy
# Always point shell_user at the install user on first write; refresh if still placeholder.
if grep -qE '^shell_user = "(REPLACE_ME|dih)"$' /etc/tty-buddy/daemon.toml 2>/dev/null \
  || ! grep -qE '^shell_user = "' /etc/tty-buddy/daemon.toml 2>/dev/null; then
  if grep -qE '^shell_user = ' /etc/tty-buddy/daemon.toml; then
    sed -i "s/^shell_user = .*/shell_user = \"$USER_NAME\"/" /etc/tty-buddy/daemon.toml
  else
    printf '\nshell_user = "%s"\n' "$USER_NAME" >>/etc/tty-buddy/daemon.toml
  fi
fi

# Template systemd unit for this machine's user (never ship a hardcoded login).
SERVICE_SRC="$HERE/systemd/tty-buddy.service"
SERVICE_DST=/etc/systemd/system/tty-buddy.service
sed "s/__TTY_BUDDY_USER__/${USER_NAME}/g" "$SERVICE_SRC" >"$SERVICE_DST"
chmod 644 "$SERVICE_DST"

install -m 644 "$HERE/udev/99-tty-buddy.rules" /etc/udev/rules.d/99-tty-buddy.rules

usermod -aG dialout,input "$USER_NAME" || true

udevadm control --reload-rules
udevadm trigger || true
systemctl daemon-reload
systemctl enable tty-buddy.service
systemctl restart tty-buddy.service || systemctl start tty-buddy.service

echo
echo "Installed tty-buddy for user '$USER_NAME'."
echo "  binary : /usr/local/bin/tty-buddy"
echo "  config : /etc/tty-buddy/"
echo "  service: systemctl status tty-buddy"
echo
echo "Next:"
echo "  1. Plug in the ESP (or unplug/replug) so udev creates /dev/tty-buddy"
echo "  2. If dialout/input are new for this session, log out/in once, then:"
echo "       sudo systemctl restart tty-buddy"
echo
echo "Optional (several boards):  tty-buddy setup --config /etc/tty-buddy/daemon.toml"
