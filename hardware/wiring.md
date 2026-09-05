# Wiring — ST7789 ↔ ESP32-C3 SuperMini

Source of truth: [`schematics/tty-buddy/`](schematics/tty-buddy/) (KiCad).
PDF: [`schematics/tty-buddy/export/tty-buddy-schematic.pdf`](schematics/tty-buddy/export/tty-buddy-schematic.pdf).

| ST7789 | ESP32-C3 SuperMini | Role |
|--------|--------------------|------|
| GND | GND | Ground |
| VCC | 3V3 | Power |
| SCL (SCK) | GPIO4 | SPI SCK |
| SDA (MOSI) | GPIO6 | SPI MOSI |
| SDA-O (MISO) | — | Not connected |
| RST | GPIO2 | Reset |
| DC | GPIO3 | Data/Command |
| CS | GPIO7 | Chip Select |
| BL | GPIO5 | Backlight (PWM); or tie to 3V3 for always-on |

| Control | ESP32-C3 SuperMini | Notes |
|---------|--------------------|--------|
| SW1 | GPIO21 → GND | Use internal pull-up in firmware |

Power: USB on SuperMini (5V pin unused on the schematic).
