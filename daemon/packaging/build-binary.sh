#!/usr/bin/env bash
# Build release binary for DEB_ARCH (amd64|arm64). Prints path to binary on stdout.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=arch.sh
source "$HERE/arch.sh"

ROOT="$(cd "$HERE/.." && pwd)"
resolve_arch "${1:-host}"

BIN_NATIVE="$ROOT/target/release/tty-buddy"

need_docker() {
  if [[ "$(uname -s)" != "Linux" ]]; then
    return 0
  fi
  if host_matches_deb_arch; then
    return 1
  fi
  return 0
}

build_native() {
  echo "==> native release ($DEB_ARCH)" >&2
  (cd "$ROOT" && cargo build --release)
  [[ -x "$BIN_NATIVE" ]] || {
    echo "missing $BIN_NATIVE" >&2
    exit 1
  }
  printf '%s\n' "$BIN_NATIVE"
}

build_docker() {
  echo "==> docker release ($DOCKER_PLATFORM / $DEB_ARCH)" >&2
  if ! docker info >/dev/null 2>&1; then
    echo "Docker is not running (needed to build $DEB_ARCH on this host)." >&2
    exit 1
  fi
  docker run --rm --platform "$DOCKER_PLATFORM" \
    -v "$ROOT:/app" -w /app \
    -v "${CARGO_HOME:-$HOME/.cargo}/registry:/usr/local/cargo/registry" \
    -v "${CARGO_HOME:-$HOME/.cargo}/git:/usr/local/cargo/git" \
    rust:1-bookworm bash -c '
      set -euo pipefail
      apt-get update -qq
      DEBIAN_FRONTEND=noninteractive apt-get install -y -qq pkg-config libudev-dev >/dev/null
      cargo build --release
    '
  [[ -x "$BIN_NATIVE" ]] || {
    echo "missing $BIN_NATIVE after docker build" >&2
    exit 1
  }
  printf '%s\n' "$BIN_NATIVE"
}

if need_docker; then
  build_docker
else
  build_native
fi
