#pragma once

#include <TFT_eSPI.h>

#include "status_snap.h"

void paintStatusGui(TFT_eSPI *tft, const StatusSnap &s, bool force_full);
bool statusGuiNeedsRoll(const StatusSnap &s);
bool statusGuiNeedsAlertTick();
void statusGuiReset();
