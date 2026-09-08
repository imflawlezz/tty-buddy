#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "terminal.h"

// Button OSD, backlight PWM, and inactivity sleep/wake.
namespace osd {

void begin(TFT_eSPI *tft, Terminal *term);
void tick(uint32_t now);
/** Dirty-row redraw only; call after content paint so wipes can be restored. */
void paint();

bool isOpen();
bool isAsleep();

} // namespace osd
