#!/usr/bin/env bash
# Glyph chart for tty-buddy (53×30). Run inside the bridge shell:
#   python3 daemon/tools/tty_bridge.py -p /dev/cu.usbmodemXXXX
#   ./daemon/tools/test_ascii.sh
set -euo pipefail

COLS="${COLUMNS:-53}"

hr() { printf '%*s\n' "$COLS" '' | tr ' ' '-'; }

putb() { printf "\\$(printf '%03o' "$1")"; }

printf '\033[2J\033[H'
printf '\033[1;36mtty-buddy glyph test (%sx%s)\033[0m\n' "$COLS" "${LINES:-30}"
hr

echo "1) Printable ASCII 0x20-0x7E (Font1):"
printf '   '
for i in $(seq 32 126); do putb "$i"; done
printf '\n\n'

echo "2) Hex grid (16 chars/row):"
for base in $(seq 32 16 112); do
  printf '   %02X ' "$base"
  for off in $(seq 0 15); do
    code=$((base + off))
    if [ "$code" -le 126 ]; then putb "$code"; else printf ' '; fi
  done
  printf '\n'
done
printf '\n'

echo "3) Shell punctuation:"
echo '   `~!@#$%^&*()-_=+[]{}|;:'"'"'",<>.?/'
echo "   quotes: 'single' \"double\" \`backtick\`"
printf '\n'

echo "4) ANSI fg colors:"
for code in 30 31 32 33 34 35 36 37; do
  printf '   \033[%sm%3d\033[0m' "$code" "$code"
  printf ' \033[%sm%3d*\033[0m' "$((code + 60))" "$((code + 60))"
done
printf '\n\n'

printf '5) Attrs: '
printf '\033[1mbold\033[0m \033[7mreverse\033[0m \033[41mredbg\033[0m \033[42mgrn\033[0m\n'
printf '\n'

echo "6) Unicode (on-device procedural — real box/block/Braille):"
echo "   blocks: ▏▎▍▌▋▊▉█ ░▒▓  ⣿⣿⣀⣿"
echo "   boxes:  ┌─┐│└─┘├─┤ ══║"
echo "   arrows: ←↑→↓ ▶◀▲▼"
echo "   misc:   •·…–— ≠≤≥ ✓✗ ×÷ °"
printf '\n'

echo "7) Tabs/spaces:"
printf '   tabs:\ta\tb\tc\n'
echo "   spaces:|     | (5)"
hr
printf '\033[1;32mDone.\033[0m Note any blank/? on the LCD and tell me the hex.\n'
