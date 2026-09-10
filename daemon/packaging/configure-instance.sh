#!/usr/bin/env bash
# Shared post-install host setup.
# Usage:
#   configure-instance.sh <username>
#   configure-instance.sh add-board <username> <serial> [id]
set -euo pipefail

sanitize_board_id() {
  # systemd instance / path id; daemon.toml keeps the raw USB serial.
  echo "$1" | tr ':/' '-'
}

add_board() {
  local USER_NAME="${1:-}"
  local SERIAL="${2:-}"
  local BOARD_ID="${3:-}"

  if [[ -z "$USER_NAME" || "$USER_NAME" == "root" ]]; then
    echo "configure-instance add-board: need a non-root username" >&2
    exit 1
  fi
  if ! id "$USER_NAME" >/dev/null 2>&1; then
    echo "configure-instance add-board: user '$USER_NAME' does not exist" >&2
    exit 1
  fi
  if [[ -z "$SERIAL" ]]; then
    echo "configure-instance add-board: need USB serial (see: tty-buddy devices)" >&2
    exit 1
  fi
  if [[ -z "$BOARD_ID" ]]; then
    BOARD_ID="$(sanitize_board_id "$SERIAL")"
  fi
  if [[ -z "$BOARD_ID" || "$BOARD_ID" == *"/"* ]]; then
    echo "configure-instance add-board: invalid board id '$BOARD_ID'" >&2
    exit 1
  fi

  local INST_DIR="/etc/tty-buddy/instances/${BOARD_ID}"
  local DAEMON_TOML="${INST_DIR}/daemon.toml"
  local BUDDY_CONFIG="${INST_DIR}/buddy.config"
  local DROPIN_DIR="/etc/systemd/system/tty-buddy-board@${BOARD_ID}.service.d"

  install -d -m 755 /etc/tty-buddy/instances
  install -d -m 755 "$INST_DIR"

  if [[ ! -f "$DAEMON_TOML" ]]; then
    cat >"$DAEMON_TOML" <<EOF
# tty-buddy board instance ${BOARD_ID}
device_path = "/dev/tty-buddy-${SERIAL}"
vid = 12346
pid = 4097
serial = "${SERIAL}"

buddy_config = "${BUDDY_CONFIG}"

shell_user = "${USER_NAME}"
EOF
    chmod 644 "$DAEMON_TOML"
  fi

  if [[ ! -f "$BUDDY_CONFIG" ]]; then
    if [[ -f /etc/tty-buddy/buddy.config ]]; then
      cp /etc/tty-buddy/buddy.config "$BUDDY_CONFIG"
    else
      cat >"$BUDDY_CONFIG" <<'EOF'
[behavior]
startup_mode = status
keyboard_opens_terminal = false
fps = 10
keyboard_layout = us
EOF
    fi
    chmod 644 "$BUDDY_CONFIG"
  fi
  chown "$USER_NAME:$USER_NAME" "$BUDDY_CONFIG"

  usermod -aG dialout,input "$USER_NAME" || true

  install -d -m 755 "$DROPIN_DIR"
  cat >"${DROPIN_DIR}/user.conf" <<EOF
[Service]
User=${USER_NAME}
Group=${USER_NAME}
EOF
  chmod 644 "${DROPIN_DIR}/user.conf"

  if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules || true
    udevadm trigger || true
  fi

  if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload || true
    systemctl enable --now "tty-buddy-board@${BOARD_ID}.service"
  fi

  echo "Configured board instance '${BOARD_ID}' for user '${USER_NAME}'."
  echo "  device : /dev/tty-buddy-${SERIAL}"
  echo "  config : ${INST_DIR}/"
  echo "  service: systemctl status tty-buddy-board@${BOARD_ID}"
}

configure_user() {
  local USER_NAME="${1:-}"
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

  if [[ -f /etc/tty-buddy/status.config && ! -f /etc/tty-buddy/buddy.config ]]; then
    mv /etc/tty-buddy/status.config /etc/tty-buddy/buddy.config
  fi

  if [[ -f /etc/tty-buddy/buddy.config ]]; then
    chown "$USER_NAME:$USER_NAME" /etc/tty-buddy/buddy.config
    chmod 644 /etc/tty-buddy/buddy.config
    if ! grep -qE '^\[behavior\]' /etc/tty-buddy/buddy.config 2>/dev/null; then
      tmp="$(mktemp)"
      {
        cat <<'EOF'
[behavior]
startup_mode = status
keyboard_opens_terminal = false
fps = 10
keyboard_layout = us

EOF
        cat /etc/tty-buddy/buddy.config
      } >"$tmp"
      mv "$tmp" /etc/tty-buddy/buddy.config
      chown "$USER_NAME:$USER_NAME" /etc/tty-buddy/buddy.config
    fi
  fi

  if [[ -f /etc/tty-buddy/status.config ]]; then
    chown "$USER_NAME:$USER_NAME" /etc/tty-buddy/status.config
    chmod 644 /etc/tty-buddy/status.config
  fi

  if grep -qE '^shell_user = "REPLACE_ME"$' /etc/tty-buddy/daemon.toml 2>/dev/null \
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
}

case "${1:-}" in
add-board)
  shift
  add_board "$@"
  ;;
-h | --help)
  cat <<'EOF'
Usage:
  configure-instance.sh <username>
  configure-instance.sh add-board <username> <serial> [id]
EOF
  exit 0
  ;;
*)
  configure_user "${1:-}"
  ;;
esac
