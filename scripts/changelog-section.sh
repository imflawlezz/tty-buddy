#!/usr/bin/env bash
# Print the Keep a Changelog body for VERSION (no ## heading, no footer links).
# Usage: changelog-section.sh 0.1.0 [CHANGELOG.md]
set -euo pipefail

VERSION="${1:?usage: $0 <version> [CHANGELOG.md]}"
FILE="${2:-CHANGELOG.md}"

if [[ ! -f "$FILE" ]]; then
  echo "missing changelog: $FILE" >&2
  exit 1
fi

# Match "## [0.1.0] - YYYY-MM-DD" or "## [0.1.0]"; skip that line; stop at next ## or [link]:
python3 - "$VERSION" "$FILE" <<'PY'
import re, sys
ver, path = sys.argv[1], sys.argv[2]
text = open(path, encoding="utf-8").read().splitlines()
heading = re.compile(rf"^## \[{re.escape(ver)}\](?:\s|$)")
out = []
grab = False
for line in text:
    if heading.match(line):
        grab = True
        continue
    if grab and (line.startswith("## ") or re.match(r"^\[[^\]]+\]:", line)):
        break
    if grab:
        out.append(line)
while out and out[0].strip() == "":
    out.pop(0)
while out and out[-1].strip() == "":
    out.pop()
body = "\n".join(out)
if not body.strip():
    sys.stderr.write(f"empty changelog section for [{ver}]\n")
    sys.exit(1)
print(body)
PY
