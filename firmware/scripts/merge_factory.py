"""After build: merge bootloader + partitions + app into dist/ (flash @ 0x0).

Writes:
  dist/tty-buddy-firmware.bin
  dist/tty-buddy-firmware-<version>.bin

Version is read from daemon/Cargo.toml (single source of truth).
"""

Import("env")
from pathlib import Path
import re
import shutil
import subprocess

PIOENV = env["PIOENV"]
if PIOENV == "native":
    Return()

PROJECT_DIR = Path(env["PROJECT_DIR"])
BUILD_DIR = Path(env.subst("$BUILD_DIR"))
OUT_DIR = PROJECT_DIR / "dist"
REPO_ROOT = PROJECT_DIR.parent


def _version() -> str:
    cargo = REPO_ROOT / "daemon" / "Cargo.toml"
    text = cargo.read_text()
    m = re.search(r'(?m)^version\s*=\s*"([^"]+)"', text)
    if not m:
        raise RuntimeError(f"version not found in {cargo}")
    return m.group(1)


def _boot_app0() -> Path:
    cand = (
        Path.home()
        / ".platformio"
        / "packages"
        / "framework-arduinoespressif32"
        / "tools"
        / "partitions"
        / "boot_app0.bin"
    )
    if cand.is_file():
        return cand
    raise FileNotFoundError(f"boot_app0.bin not found at {cand}")


def _esptool() -> list:
    pkg = Path.home() / ".platformio" / "packages" / "tool-esptoolpy" / "esptool.py"
    if pkg.is_file():
        return [env.subst("$PYTHONEXE"), str(pkg)]
    if shutil.which("esptool.py"):
        return ["esptool.py"]
    if shutil.which("esptool"):
        return ["esptool"]
    raise FileNotFoundError("esptool not found")


def merge_factory(source, target, env):  # noqa: ARG001
    parts = {
        "bootloader": BUILD_DIR / "bootloader.bin",
        "partitions": BUILD_DIR / "partitions.bin",
        "firmware": BUILD_DIR / "firmware.bin",
    }
    for path in parts.values():
        if not path.is_file():
            print(f"[merge_factory] skip: missing {path}")
            return

    boot0 = _boot_app0()
    version = _version()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out_plain = OUT_DIR / "tty-buddy-firmware.bin"
    out_ver = OUT_DIR / f"tty-buddy-firmware-{version}.bin"

    cmd = [
        *_esptool(),
        "--chip",
        "esp32c3",
        "merge_bin",
        "-o",
        str(out_plain),
        "--flash_mode",
        "dio",
        "--flash_freq",
        "80m",
        "--flash_size",
        "4MB",
        "0x0000",
        str(parts["bootloader"]),
        "0x8000",
        str(parts["partitions"]),
        "0xe000",
        str(boot0),
        "0x10000",
        str(parts["firmware"]),
    ]
    print(f"[merge_factory] {' '.join(cmd)}")
    subprocess.check_call(cmd)
    shutil.copyfile(out_plain, out_ver)
    size = out_plain.stat().st_size
    print(f"[merge_factory] wrote {out_plain} and {out_ver.name} ({size} bytes) — flash at 0x0")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_factory)
