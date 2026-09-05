"""Fix TFT_eSPI ESP32-C3 SPI_PORT mismatch (SPI2_HOST=1 vs REG_SPI_BASE expects 2)."""

Import("env")
from pathlib import Path

header = (
    Path(env["PROJECT_DIR"])
    / ".pio"
    / "libdeps"
    / env["PIOENV"]
    / "TFT_eSPI"
    / "Processors"
    / "TFT_eSPI_ESP32_C3.h"
)

if not header.exists():
    print(f"[fix_tft_espi_c3] skip: {header} not found yet")
else:
    text = header.read_text()
    old = "#define SPI_PORT SPI2_HOST"
    new = (
        "// tty-buddy: REG_SPI_BASE(i) expects i==2 on C3, but SPI2_HOST==1\n"
        "#define SPI_PORT 2"
    )
    if old in text:
        header.write_text(text.replace(old, new, 1))
        print(f"[fix_tft_espi_c3] patched {header}")
    elif "SPI_PORT 2" in text:
        print("[fix_tft_espi_c3] already patched")
    else:
        print("[fix_tft_espi_c3] WARNING: expected SPI_PORT define not found")
