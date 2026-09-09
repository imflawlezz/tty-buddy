#!/usr/bin/env bash
# Write dist/checksums.txt (SHA-256, GNU sha256sum format) for release artifacts.
# Usage: write-checksums.sh [dist-dir]
set -euo pipefail

DIR="${1:-dist}"
if [[ ! -d "$DIR" ]]; then
  echo "missing directory: $DIR" >&2
  exit 1
fi

cd "$DIR"
rm -f checksums.txt

shopt -s nullglob
files=(
  tty-buddy-firmware-*.bin
  tty-buddy_*.deb
  tty-buddy-*-linux.tar.gz
)
if [[ ${#files[@]} -eq 0 ]]; then
  echo "no release artifacts in $DIR" >&2
  exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "${files[@]}" | sort -k2 >checksums.txt
elif command -v shasum >/dev/null 2>&1; then
  # BSD/macOS: emit the same "HASH  NAME" layout as sha256sum.
  for f in "${files[@]}"; do
    printf '%s  %s\n' "$(shasum -a 256 "$f" | awk '{print $1}')" "$f"
  done | sort -k2 >checksums.txt
else
  echo "need sha256sum or shasum" >&2
  exit 1
fi

echo "==> $(pwd)/checksums.txt"
cat checksums.txt
