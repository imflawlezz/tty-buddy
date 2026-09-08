#pragma once

#include <TFT_eSPI.h>

#include "status_snap.h"

void paintStatusGui(TFT_eSPI *tft, const StatusSnap &s, bool force_full);
bool statusGuiNeedsRoll(const StatusSnap &s);
bool statusGuiNeedsAlertTick();
void statusGuiReset();

/** Alert strip currently visible. */
bool statusGuiAlertActive();
/** Hide the current alert until its text changes. */
void statusGuiDismissAlert();

/** Skip painting this pixel rect (OSD). Zero size clears. */
void statusGuiSetOverlay(int x, int y, int w, int h);
