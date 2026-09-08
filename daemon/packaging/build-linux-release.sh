#!/usr/bin/env bash
# Build tty-buddy-*-x86_64-linux.tar.gz on this machine.
# macOS: Docker Desktop. Linux x86_64: native cargo.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/dist"
VER="$(grep '^version' "$ROOT/Cargo.toml" | head -1 | cut -d'"' -f2)"
NAME="tty-buddy-${VER}-x86_64-linux"

mkdir -p "$OUT/$NAME/bin" "$OUT/$NAME/etc" "$OUT/$NAME/systemd" "$OUT/$NAME/udev"

build_native() {
  echo "==> native release build"
  (cd "$ROOT" && cargo build --release)
}

build_docker() {
  echo "==> cross-build (docker rust:bookworm)"
  if ! docker info >/dev/null 2>&1; then
    echo "Docker is not running. Start Docker Desktop, then re-run." >&2
    exit 1
  fi
  docker run --rm \
    -v "$ROOT:/app" -w /app \
    -v "$HOME/.cargo/registry:/usr/local/cargo/registry" \
    -v "$HOME/.cargo/git:/usr/local/cargo/git" \
    rust:1-bookworm bash -c '
      apt-get update -qq
      DEBIAN_FRONTEND=noninteractive apt-get install -y -qq pkg-config libudev-dev >/dev/null
      cargo build --release
    '
}

ARCH="$(uname -m)"
OS="$(uname -s)"
if [[ "$OS" == "Linux" && ( "$ARCH" == "x86_64" || "$ARCH" == "amd64" ) ]]; then
  build_native
else
  build_docker
fi

BIN="$ROOT/target/release/tty-buddy"
[[ -x "$BIN" ]] || { echo "missing $BIN" >&2; exit 1; }

cp "$BIN" "$OUT/$NAME/bin/"
cp "$ROOT/status.config" "$OUT/$NAME/etc/"
cp "$ROOT/packaging/daemon.toml.example" "$OUT/$NAME/etc/daemon.toml"
cp "$ROOT/packaging/tty-buddy.service" "$OUT/$NAME/systemd/"
cp "$ROOT/udev/99-tty-buddy.rules" "$OUT/$NAME/udev/"
cp "$ROOT/packaging/install.sh" "$OUT/$NAME/"
cp "$ROOT/packaging/flash-firmware.sh" "$OUT/$NAME/"
chmod +x "$OUT/$NAME/bin/tty-buddy" "$OUT/$NAME/install.sh" "$OUT/$NAME/flash-firmware.sh"

tar -C "$OUT" -czf "$OUT/$NAME.tar.gz" "$NAME"
echo "==> $OUT/$NAME.tar.gz"
ls -lh "$OUT/$NAME.tar.gz"
