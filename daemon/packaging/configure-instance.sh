#!/usr/bin/env bash
# Shared post-install host setup. Usage: configure-instance.sh <username>
set -euo pipefail

USER_NAME="${1:-}"
if [[ -z "$USER_NAME" || "$USER_NAME" == "root" ]]; then
  echo "configure-instance: need a non-root username" >&2
  exit 1
fi
if ! id "$USER_NAME" >/dev/null 2>&1; then
  echo "configure-instance: user '$USER_NAME' does not exist" >&2
  exit 1
fi

if [[ ! -f /etc/tty-buddy/daemon.toml ]]; then
  echo "configure-instance: missing /etc/tty-buddy/daemon.toml" >&2
  exit 1
fi

chown root:root /etc/tty-buddy
chmod 755 /etc/tty-buddy

if [[ -f /etc/tty-buddy/status.config ]]; then
  chown "$USER_NAME:$USER_NAME" /etc/tty-buddy/status.config
  chmod 644 /etc/tty-buddy/status.config
fi

if grep -qE '^shell_user = "(REPLACE_ME|dih)"$' /etc/tty-buddy/daemon.toml 2>/dev/null \
  || ! grep -qE '^shell_user = "' /etc/tty-buddy/daemon.toml 2>/dev/null; then
  if grep -qE '^shell_user = ' /etc/tty-buddy/daemon.toml; then
    sed -i "s/^shell_user = .*/shell_user = \"$USER_NAME\"/" /etc/tty-buddy/daemon.toml
  else
    printf '\nshell_user = "%s"\n' "$USER_NAME" >>/etc/tty-buddy/daemon.toml
  fi
fi

usermod -aG dialout,input "$USER_NAME" || true

if command -v udevadm >/dev/null 2>&1; then
  udevadm control --reload-rules || true
  udevadm trigger || true
fi

if command -v systemctl >/dev/null 2>&1; then
  systemctl daemon-reload || true
  # Pre-template installs used a fixed tty-buddy.service.
  if systemctl cat tty-buddy.service >/dev/null 2>&1; then
    systemctl disable --now tty-buddy.service 2>/dev/null || true
  fi
  systemctl enable --now "tty-buddy@${USER_NAME}.service"
fi

echo "Configured tty-buddy for user '$USER_NAME' (tty-buddy@${USER_NAME})."
