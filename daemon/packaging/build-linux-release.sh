#!/usr/bin/env bash
# Build tty-buddy-<ver>-<x86_64|aarch64>-linux.tar.gz into daemon/dist/.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=arch.sh
source "$HERE/arch.sh"

ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/dist"
VER="$(grep '^version' "$ROOT/Cargo.toml" | head -1 | cut -d'"' -f2)"
resolve_arch "${1:-host}"

NAME="tty-buddy-${VER}-${TAR_ARCH}-linux"
STAGE="$OUT/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/etc" "$STAGE/systemd" "$STAGE/udev" "$STAGE/lib"

BIN="$("$HERE/build-binary.sh" "$DEB_ARCH")"
cp "$BIN" "$STAGE/bin/tty-buddy"
cp "$ROOT/buddy.config" "$STAGE/etc/"
cp "$ROOT/packaging/daemon.toml.example" "$STAGE/etc/daemon.toml"
cp "$ROOT/packaging/tty-buddy@.service" "$STAGE/systemd/"
cp "$ROOT/packaging/tty-buddy-board@.service" "$STAGE/systemd/"
cp "$ROOT/udev/99-tty-buddy.rules" "$STAGE/udev/"
cp "$ROOT/packaging/install.sh" "$STAGE/"
cp "$ROOT/packaging/uninstall.sh" "$STAGE/"
cp "$ROOT/packaging/configure-instance.sh" "$STAGE/lib/"
chmod +x "$STAGE/bin/tty-buddy" "$STAGE/install.sh" "$STAGE/uninstall.sh" \
  "$STAGE/lib/configure-instance.sh"

tar -C "$OUT" -czf "$OUT/$NAME.tar.gz" "$NAME"
rm -rf "$STAGE"
echo "==> $OUT/$NAME.tar.gz"
ls -lh "$OUT/$NAME.tar.gz"

if [[ -n "${DEST:-}" && "$DEST" != "$OUT" ]]; then
  mkdir -p "$DEST"
  cp -f "$OUT/$NAME.tar.gz" "$DEST/"
fi
