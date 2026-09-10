#pragma once

#include <stdint.h>

/// Fill 6×8 MSB-left rows for curated PL/DE diacritics; false if unknown/empty.
bool glyphUnicodeRows(uint16_t cp, uint8_t out[8]);
