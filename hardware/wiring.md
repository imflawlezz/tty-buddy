# Wiring — ST7789 ↔ ESP32-C3 SuperMini

Nets match [`schematics/tty-buddy/`](schematics/tty-buddy/) and firmware [`include/User_Setup.h`](../firmware/include/User_Setup.h) /
[`src/osd.cpp`](../firmware/src/osd.cpp).

PDF: [`schematics/tty-buddy/export/tty-buddy-schematic.pdf`](schematics/tty-buddy/export/tty-buddy-schematic.pdf).

| ST7789 | ESP32-C3 SuperMini | Role |
|--------|--------------------|------|
| GND | GND | Ground |
| VCC | 3V3 | Power |
| SCL (SCK) | GPIO4 | SPI clock |
| SDA (MOSI) | GPIO6 | SPI data |
| SDA-O (MISO) | — | Not connected |
| RST | GPIO2 | Reset |
| DC | GPIO3 | Data / command |
| CS | GPIO7 | Chip select |
| BL | GPIO5 | Backlight PWM |

| Control | ESP32-C3 SuperMini | Notes |
|---------|--------------------|--------|
| SW1 | GPIO10 → GND | Firmware uses internal pull-up |

Power: USB on the SuperMini.
