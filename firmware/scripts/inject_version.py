"""Inject TTY_BUDDY_VERSION + build date from daemon/Cargo.toml into the firmware build."""

Import("env")
from datetime import datetime, timezone
from pathlib import Path
import re

cargo = Path(env["PROJECT_DIR"]).parent / "daemon" / "Cargo.toml"
ver = "0.0.0"
if cargo.is_file():
    m = re.search(r'(?m)^version\s*=\s*"([^"]+)"', cargo.read_text())
    if m:
        ver = m.group(1)

# DD.MM.YY in local time.
build_date = datetime.now().astimezone().strftime("%d.%m.%y")
label = f"{ver}/{build_date}"

env.Append(CPPDEFINES=[("TTY_BUDDY_VERSION", env.StringifyMacro(label))])
print(f"[inject_version] TTY_BUDDY_VERSION={label}")
