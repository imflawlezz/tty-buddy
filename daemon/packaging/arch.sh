#!/usr/bin/env bash
# Resolve packaging arch / Rust target / tarball arch triple fragment.
# Usage:
#   source arch.sh
#   resolve_arch amd64   # or arm64 / host / empty(=host)
# Sets: DEB_ARCH RUST_TARGET TAR_ARCH DOCKER_PLATFORM

resolve_arch() {
  local want="${1:-host}"
  local host_m
  host_m="$(uname -m)"

  case "$want" in
  host | "")
    case "$host_m" in
    x86_64 | amd64) want=amd64 ;;
    aarch64 | arm64) want=arm64 ;;
    *)
      echo "unsupported host arch: $host_m" >&2
      return 1
      ;;
    esac
    ;;
  esac

  case "$want" in
  amd64 | x86_64)
    DEB_ARCH=amd64
    RUST_TARGET=x86_64-unknown-linux-gnu
    TAR_ARCH=x86_64
    DOCKER_PLATFORM=linux/amd64
    ;;
  arm64 | aarch64)
    DEB_ARCH=arm64
    RUST_TARGET=aarch64-unknown-linux-gnu
    TAR_ARCH=aarch64
    DOCKER_PLATFORM=linux/arm64
    ;;
  *)
    echo "arch must be amd64 or arm64 (got: $want)" >&2
    return 1
    ;;
  esac
}

host_matches_deb_arch() {
  local host_m
  host_m="$(uname -m)"
  case "$DEB_ARCH" in
  amd64) [[ "$host_m" == "x86_64" || "$host_m" == "amd64" ]] ;;
  arm64) [[ "$host_m" == "aarch64" || "$host_m" == "arm64" ]] ;;
  *) return 1 ;;
  esac
}
