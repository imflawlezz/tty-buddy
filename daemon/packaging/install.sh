#!/usr/bin/env bash
# Install from a release tree. Run as root: sudo ./install.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"

install -d /usr/local/bin /etc/tty-buddy /etc/systemd/system /etc/udev/rules.d
install -m 755 "$HERE/bin/tty-buddy" /usr/local/bin/tty-buddy

if [[ ! -f /etc/tty-buddy/status.config ]]; then
  install -m 644 "$HERE/etc/status.config" /etc/tty-buddy/status.config
fi
if [[ ! -f /etc/tty-buddy/daemon.toml ]]; then
  install -m 644 "$HERE/etc/daemon.toml" /etc/tty-buddy/daemon.toml
  USER_NAME="${SUDO_USER:-dih}"
  sed -i "s/^shell_user = .*/shell_user = \"$USER_NAME\"/" /etc/tty-buddy/daemon.toml || true
fi

install -m 644 "$HERE/systemd/tty-buddy.service" /etc/systemd/system/tty-buddy.service
install -m 644 "$HERE/udev/99-tty-buddy.rules" /etc/udev/rules.d/99-tty-buddy.rules

USER_NAME="${SUDO_USER:-dih}"
usermod -aG dialout,input "$USER_NAME" || true

udevadm control --reload-rules
udevadm trigger
systemctl daemon-reload
systemctl enable tty-buddy.service
systemctl restart tty-buddy.service || systemctl start tty-buddy.service

echo
echo "Installed tty-buddy."
echo "  binary : /usr/local/bin/tty-buddy"
echo "  config : /etc/tty-buddy/"
echo "  service: systemctl status tty-buddy"
echo
echo "If this is the first install, log out/in so dialout+input groups apply,"
echo "then:  sudo systemctl restart tty-buddy"
echo "Optional device pick:  tty-buddy setup --config /etc/tty-buddy/daemon.toml"
