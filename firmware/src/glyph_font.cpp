#include "glyph_font.h"

#include <string.h>

#include "glyph_unicode_data.h"

bool glyphUnicodeRows(uint16_t cp, uint8_t out[8]) {
  memset(out, 0, 8);
  for (unsigned i = 0; i < kGlyphPlDeCount; i++) {
    uint16_t entry_cp = pgm_read_word(&kGlyphPlDe[i].cp);
    if (entry_cp != cp)
      continue;
    bool any = false;
    for (int r = 0; r < 8; r++) {
      uint8_t b = pgm_read_byte(&kGlyphPlDe[i].rows[r]);
      out[r] = b;
      if (b)
        any = true;
    }
    return any;
  }
  return false;
}
