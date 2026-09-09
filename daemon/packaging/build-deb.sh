#!/usr/bin/env bash
# Build tty-buddy_<ver>_<amd64|arm64>.deb into daemon/dist/ (and optional DEST).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=arch.sh
source "$HERE/arch.sh"

ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/dist"
VER="$(grep '^version' "$ROOT/Cargo.toml" | head -1 | cut -d'"' -f2)"
resolve_arch "${1:-host}"

if ! command -v dpkg-deb >/dev/null 2>&1; then
  echo "dpkg-deb not found. Install dpkg (Debian/Ubuntu) or use Docker." >&2
  # Host lacks dpkg-deb (e.g. macOS): build the package inside Debian.
  if ! docker info >/dev/null 2>&1; then
    exit 1
  fi
  DEST_HOST="${DEST:-}"
  docker run --rm --platform "$DOCKER_PLATFORM" \
    -v "$ROOT:/app" -w /app/packaging \
    -e "DEST=/app/dist" \
    -v "${CARGO_HOME:-$HOME/.cargo}/registry:/usr/local/cargo/registry" \
    -v "${CARGO_HOME:-$HOME/.cargo}/git:/usr/local/cargo/git" \
    rust:1-bookworm bash -c "
      set -euo pipefail
      apt-get update -qq
      DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
        pkg-config libudev-dev dpkg-dev >/dev/null
      ./build-deb.sh $DEB_ARCH
    "
  DEB="$OUT/tty-buddy_${VER}_${DEB_ARCH}.deb"
  [[ -f "$DEB" ]] || {
    echo "missing $DEB" >&2
    exit 1
  }
  if [[ -n "${DEST_HOST:-}" && "$DEST_HOST" != "$OUT" ]]; then
    mkdir -p "$DEST_HOST"
    cp -f "$DEB" "$DEST_HOST/"
  fi
  ls -lh "$DEB"
  exit 0
fi

BIN="$("$HERE/build-binary.sh" "$DEB_ARCH")"
PKG_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/tty-buddy-deb.XXXXXX")"
cleanup() { rm -rf "$PKG_ROOT"; }
trap cleanup EXIT

mkdir -p \
  "$PKG_ROOT/DEBIAN" \
  "$PKG_ROOT/usr/bin" \
  "$PKG_ROOT/usr/lib/tty-buddy" \
  "$PKG_ROOT/etc/tty-buddy" \
  "$PKG_ROOT/usr/lib/systemd/system" \
  "$PKG_ROOT/etc/udev/rules.d"

install -m 755 "$BIN" "$PKG_ROOT/usr/bin/tty-buddy"
install -m 644 "$ROOT/status.config" "$PKG_ROOT/etc/tty-buddy/status.config"
install -m 644 "$ROOT/packaging/daemon.toml.example" "$PKG_ROOT/etc/tty-buddy/daemon.toml"
install -m 644 "$ROOT/packaging/tty-buddy@.service" "$PKG_ROOT/usr/lib/systemd/system/tty-buddy@.service"
install -m 644 "$ROOT/udev/99-tty-buddy.rules" "$PKG_ROOT/etc/udev/rules.d/99-tty-buddy.rules"
install -m 755 "$ROOT/packaging/configure-instance.sh" "$PKG_ROOT/usr/lib/tty-buddy/configure-instance.sh"

SIZE_KB="$(du -sk "$PKG_ROOT" | cut -f1)"

cat >"$PKG_ROOT/DEBIAN/control" <<EOF
Package: tty-buddy
Version: $VER
Architecture: $DEB_ARCH
Maintainer: tty-buddy contributors <https://github.com/imflawlezz/tty-buddy>
Section: utils
Priority: optional
Depends: libudev1, systemd, passwd, adduser
Installed-Size: $SIZE_KB
Homepage: https://github.com/imflawlezz/tty-buddy
Description: ESP32 status display and PTY console bridge
 Host daemon for the tty-buddy ESP32-C3 + ST7789 buddy: live status UI,
 console PTY, and USB keyboard grab over USB-JTAG serial.
EOF

cat >"$PKG_ROOT/DEBIAN/conffiles" <<EOF
/etc/tty-buddy/daemon.toml
/etc/tty-buddy/status.config
EOF

cat >"$PKG_ROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
  USER_NAME="${TTY_BUDDY_USER:-${SUDO_USER:-}}"
  if [ -n "$USER_NAME" ] && [ "$USER_NAME" != root ] && id "$USER_NAME" >/dev/null 2>&1; then
    /usr/lib/tty-buddy/configure-instance.sh "$USER_NAME"
  else
    echo "tty-buddy: package installed. Finish setup as your login user:"
    echo "  sudo TTY_BUDDY_USER=\"\$USER\" /usr/lib/tty-buddy/configure-instance.sh \"\$USER\""
    echo "  # or:  sudo systemctl enable --now tty-buddy@\$USER"
    if command -v systemctl >/dev/null 2>&1; then
      systemctl daemon-reload || true
    fi
    if command -v udevadm >/dev/null 2>&1; then
      udevadm control --reload-rules || true
      udevadm trigger || true
    fi
  fi
fi
EOF
chmod 755 "$PKG_ROOT/DEBIAN/postinst"

cat >"$PKG_ROOT/DEBIAN/prerm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = remove ] || [ "$1" = deconfigure ]; then
  if command -v systemctl >/dev/null 2>&1; then
    systemctl disable --now tty-buddy.service 2>/dev/null || true
    for unit in $(systemctl list-units --type=service --all --plain --no-legend 'tty-buddy@*' 2>/dev/null | awk '{print $1}'); do
      systemctl disable --now "$unit" 2>/dev/null || true
    done
  fi
fi
EOF
chmod 755 "$PKG_ROOT/DEBIAN/prerm"

cat >"$PKG_ROOT/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if command -v systemctl >/dev/null 2>&1; then
  systemctl daemon-reload || true
fi
EOF
chmod 755 "$PKG_ROOT/DEBIAN/postrm"

mkdir -p "$OUT"
DEB="$OUT/tty-buddy_${VER}_${DEB_ARCH}.deb"
dpkg-deb --root-owner-group --build "$PKG_ROOT" "$DEB"
echo "==> $DEB"
ls -lh "$DEB"

if [[ -n "${DEST:-}" && "$DEST" != "$OUT" ]]; then
  mkdir -p "$DEST"
  cp -f "$DEB" "$DEST/"
fi
