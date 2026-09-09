# Hardware

ESP32-C3 SuperMini + ST7789 320×240 panel, one front button, PWM backlight.

## Contents

| Path | Contents |
|------|----------|
| [`schematics/tty-buddy/`](schematics/tty-buddy/) | KiCad project (schematic + symbol lib) |
| [`schematics/tty-buddy/export/tty-buddy-schematic.pdf`](schematics/tty-buddy/export/tty-buddy-schematic.pdf) | Schematic PDF |
| [`wiring.md`](wiring.md) | Pin table matching firmware `User_Setup.h` / OSD pins |
| [`cad/`](cad/) | Enclosure (`tty-buddy-enclosure.f3d`) and exports under `cad/export/` (STEP / STL / 3MF) |

## Electrical summary

- **Display:** SPI ST7789 — SCK GPIO4, MOSI GPIO6, CS GPIO7, DC GPIO3, RST
  GPIO2.
- **Backlight:** GPIO5 PWM (firmware). May be tied to 3V3 for always-on (no
  dimming).
- **Button:** GPIO10 to GND, internal pull-up (`INPUT_PULLUP` in firmware).
- **Power:** USB on the SuperMini. Schematic does not use the board 5V pin
  for the panel.

Full pin table: [`wiring.md`](wiring.md). Source of truth for nets: the KiCad schematic.

## Firmware alignment

Pins are fixed in:

- [`firmware/include/User_Setup.h`](../firmware/include/User_Setup.h) — TFT SPI
- [`firmware/src/osd.cpp`](../firmware/src/osd.cpp) — `PIN_BL = 5`, `PIN_BTN = 10`

Rotation and colour order assume a 320×240 module with `TFT_BGR` and
`TFT_INVERSION_OFF` as configured in `User_Setup.h`.
