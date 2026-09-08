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

/**
 * After OSD closes: fill `x,y,w,h` with status bg and repaint only widgets
 * that intersect that rect (no full-screen wipe).
 */
void statusGuiRestoreRegion(TFT_eSPI *tft, const StatusSnap &s, int x, int y,
                            int w, int h);

