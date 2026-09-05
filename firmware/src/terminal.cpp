#include "terminal.h"

#include <string.h>

static const uint16_t kAnsi16[16] = {
    0x0000, 0xA800, 0x0540, 0xAB40, 0x0015, 0xA815, 0x0555, 0xAD55,
    0x52AA, 0xF800, 0x07E0, 0xFFE0, 0x001F, 0xF81F, 0x07FF, 0xFFFF,
};

static constexpr uint8_t kCx = 0x08; // vertical bar column
static constexpr uint8_t kHx = 0x3F; // full horizontal
static constexpr int kMid = 3;

static void fillRows(uint8_t *out, int y0, int y1, uint8_t mask) {
  memset(out, 0, TERM_CELL_H);
  if (y0 < 0)
    y0 = 0;
  if (y1 > TERM_CELL_H)
    y1 = TERM_CELL_H;
  for (int y = y0; y < y1; y++)
    out[y] = mask;
}

static bool glyphBox(uint16_t cp, uint8_t *out) {
  if (cp < 0x2500 || cp > 0x257F)
    return false;
  memset(out, 0, TERM_CELL_H);

  auto vline = [&]() {
    for (int y = 0; y < TERM_CELL_H; y++)
      out[y] = kCx;
  };
  auto hline = [&](uint8_t mask = kHx) { out[kMid] = mask; };

  if (cp == 0x2500 || cp == 0x2501 || cp == 0x2504 || cp == 0x2505 ||
      cp == 0x2508 || cp == 0x2509 || cp == 0x254C || cp == 0x254D ||
      cp == 0x2550) {
    hline();
    return true;
  }
  if (cp == 0x2502 || cp == 0x2503 || cp == 0x2506 || cp == 0x2507 ||
      cp == 0x250A || cp == 0x250B || cp == 0x254E || cp == 0x254F ||
      cp == 0x2551) {
    vline();
    return true;
  }

  struct Corner {
    uint16_t cp;
    bool down;
    bool right;
  };
  static const Corner corners[] = {
      {0x250C, true, true},  {0x250F, true, true},  {0x2552, true, true},
      {0x2553, true, true},  {0x2554, true, true},  {0x2510, true, false},
      {0x2513, true, false}, {0x2555, true, false}, {0x2556, true, false},
      {0x2557, true, false}, {0x2514, false, true}, {0x2517, false, true},
      {0x2558, false, true}, {0x2559, false, true}, {0x255A, false, true},
      {0x2518, false, false},{0x251B, false, false},{0x255B, false, false},
      {0x255C, false, false},{0x255D, false, false},
  };
  for (const auto &c : corners) {
    if (c.cp != cp)
      continue;
    uint8_t h = c.right ? 0x0F : 0x38;
    out[kMid] = h | kCx;
    if (c.down) {
      for (int y = kMid; y < TERM_CELL_H; y++)
        out[y] |= kCx;
    } else {
      for (int y = 0; y <= kMid; y++)
        out[y] |= kCx;
    }
    out[kMid] = h | kCx;
    return true;
  }

  // Light and double variants share the same 6×8 topology.
  if (cp == 0x253C || cp == 0x256A || cp == 0x256B || cp == 0x256C) {
    vline();
    out[kMid] = kHx;
    return true;
  }
  if (cp == 0x251C || cp == 0x255E || cp == 0x255F || cp == 0x2560) { // ├
    vline();
    out[kMid] = 0x0F | kCx;
    return true;
  }
  if (cp == 0x2524 || cp == 0x2561 || cp == 0x2562 || cp == 0x2563) { // ┤
    vline();
    out[kMid] = 0x38 | kCx;
    return true;
  }
  if (cp == 0x252C || cp == 0x2564 || cp == 0x2565 || cp == 0x2566) { // ┬
    hline();
    for (int y = kMid; y < TERM_CELL_H; y++)
      out[y] |= kCx;
    return true;
  }
  if (cp == 0x2534 || cp == 0x2567 || cp == 0x2568 || cp == 0x2569) { // ┴
    hline();
    for (int y = 0; y <= kMid; y++)
      out[y] |= kCx;
    return true;
  }

  if (cp == 0x2571) {
    uint8_t r[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0, 0};
    memcpy(out, r, 8);
    return true;
  }
  if (cp == 0x2572) {
    uint8_t r[8] = {0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0, 0};
    memcpy(out, r, 8);
    return true;
  }
  if (cp == 0x2573) {
    uint8_t r[8] = {0x21, 0x12, 0x0C, 0x0C, 0x12, 0x21, 0, 0};
    memcpy(out, r, 8);
    return true;
  }
  if (cp == 0x2574) {
    hline(0x38);
    return true;
  }
  if (cp == 0x2575) {
    for (int y = 0; y <= kMid; y++)
      out[y] = kCx;
    return true;
  }
  if (cp == 0x2576) {
    hline(0x0F);
    return true;
  }
  if (cp == 0x2577) {
    for (int y = kMid; y < TERM_CELL_H; y++)
      out[y] = kCx;
    return true;
  }

  // Prefer a visible cross over a blank cell for unmapped box codepoints.
  vline();
  out[kMid] = kHx;
  return true;
}

static bool glyphBlock(uint16_t cp, uint8_t *out) {
  if (cp == 0x25A0 || cp == 0x25AE || cp == 0x25FC || cp == 0x25FE) {
    fillRows(out, 1, 7, 0x1E);
    return true;
  }
  if (cp == 0x25AA) {
    fillRows(out, 2, 6, 0x0C);
    return true;
  }
  if (cp < 0x2580 || cp > 0x259F)
    return false;

  if (cp == 0x2580) {
    fillRows(out, 0, 4, kHx);
    return true;
  }
  if (cp >= 0x2581 && cp <= 0x2587) {
    int n = (int)(cp - 0x2580);
    int h = (n * TERM_CELL_H) / 8;
    if (h < 1)
      h = 1;
    fillRows(out, TERM_CELL_H - h, TERM_CELL_H, kHx);
    return true;
  }
  if (cp == 0x2584) {
    fillRows(out, 4, 8, kHx);
    return true;
  }

  static const uint8_t left_w[] = {
      /*2588*/ 6, 5, 4, 3, 3, 2, 1, 1 /*258F*/
  };
  if (cp >= 0x2588 && cp <= 0x258F) {
    uint8_t w = left_w[cp - 0x2588];
    uint8_t mask = (uint8_t)((0x3F << (6 - w)) & 0x3F);
    fillRows(out, 0, TERM_CELL_H, mask);
    return true;
  }
  if (cp == 0x2590) {
    fillRows(out, 0, TERM_CELL_H, 0x07);
    return true;
  }
  if (cp == 0x2591) {
    uint8_t r[8] = {0x0A, 0x00, 0x15, 0x00, 0x0A, 0x00, 0x15, 0x00};
    memcpy(out, r, 8);
    return true;
  }
  if (cp == 0x2592) {
    uint8_t r[8] = {0x2A, 0x15, 0x2A, 0x15, 0x2A, 0x15, 0x2A, 0x15};
    memcpy(out, r, 8);
    return true;
  }
  if (cp == 0x2593) {
    uint8_t r[8] = {0x3E, 0x1D, 0x3E, 0x1D, 0x3E, 0x1D, 0x3E, 0x1D};
    memcpy(out, r, 8);
    return true;
  }
  if (cp == 0x2594) {
    fillRows(out, 0, 1, kHx);
    return true;
  }
  if (cp == 0x2595) {
    fillRows(out, 0, TERM_CELL_H, 0x01);
    return true;
  }
  if (cp >= 0x2596 && cp <= 0x259F) {
    fillRows(out, 0, TERM_CELL_H, 0x3C);
    return true;
  }
  fillRows(out, 0, TERM_CELL_H, kHx);
  return true;
}

static bool glyphBraille(uint16_t cp, uint8_t *out) {
  if (cp < 0x2800 || cp > 0x28FF)
    return false;
  memset(out, 0, TERM_CELL_H);
  uint16_t bits = cp - 0x2800;
  // Unicode Braille dot order → pixel (row, column bit) in the 6×8 cell.
  static const int8_t pos[8][2] = {
      {0, 1}, {2, 1}, {4, 1}, {0, 3}, {2, 3}, {4, 3}, {6, 1}, {6, 3},
  };
  for (int i = 0; i < 8; i++) {
    if (!(bits & (1u << i)))
      continue;
    int y = pos[i][0];
    int xb = pos[i][1];
    uint8_t bit = (uint8_t)(1u << (TERM_CELL_W - 1 - xb));
    out[y] |= bit;
    if (y + 1 < TERM_CELL_H)
      out[y + 1] |= bit;
  }
  return true;
}

static bool glyphArrowEtc(uint16_t cp, uint8_t *out) {
  static const uint8_t kLeft[8] = {0x00, 0x08, 0x10, 0x3F, 0x10, 0x08, 0x00, 0x00};
  static const uint8_t kRight[8] = {0x00, 0x04, 0x02, 0x3F, 0x02, 0x04, 0x00, 0x00};
  static const uint8_t kUp[8] = {0x00, 0x08, 0x1C, 0x2A, 0x08, 0x08, 0x08, 0x00};
  static const uint8_t kDown[8] = {0x00, 0x08, 0x08, 0x08, 0x2A, 0x1C, 0x08, 0x00};
  static const uint8_t kTriU[8] = {0x00, 0x08, 0x1C, 0x1C, 0x3E, 0x3E, 0x00, 0x00};
  static const uint8_t kTriD[8] = {0x00, 0x3E, 0x3E, 0x1C, 0x1C, 0x08, 0x00, 0x00};
  static const uint8_t kTriR[8] = {0x00, 0x20, 0x30, 0x38, 0x30, 0x20, 0x00, 0x00};
  static const uint8_t kTriL[8] = {0x00, 0x02, 0x06, 0x0E, 0x06, 0x02, 0x00, 0x00};
  static const uint8_t kCheck[8] = {0x00, 0x02, 0x04, 0x28, 0x10, 0x00, 0x00, 0x00};
  static const uint8_t kCross[8] = {0x00, 0x22, 0x14, 0x08, 0x14, 0x22, 0x00, 0x00};

  memset(out, 0, TERM_CELL_H);
  switch (cp) {
  case 0x2190:
    memcpy(out, kLeft, 8);
    return true;
  case 0x2192:
    memcpy(out, kRight, 8);
    return true;
  case 0x2191:
    memcpy(out, kUp, 8);
    return true;
  case 0x2193:
    memcpy(out, kDown, 8);
    return true;
  case 0x25B2:
    memcpy(out, kTriU, 8);
    return true;
  case 0x25BC:
    memcpy(out, kTriD, 8);
    return true;
  case 0x25B6:
    memcpy(out, kTriR, 8);
    return true;
  case 0x25C0:
    memcpy(out, kTriL, 8);
    return true;
  case 0x2022:
  case 0x00B7:
    fillRows(out, 3, 5, 0x0C);
    return true;
  case 0x2026:
    out[6] = 0x2A;
    return true;
  case 0x2713:
  case 0x2714:
    memcpy(out, kCheck, 8);
    return true;
  case 0x2717:
  case 0x2718:
    memcpy(out, kCross, 8);
    return true;
  default:
    return false;
  }
}

uint16_t Terminal::crc16(const uint8_t *data, size_t n, uint16_t seed) {
  uint16_t crc = seed;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

void Terminal::begin(TFT_eSPI *display) {
  tft_ = display;
  reset();
}

void Terminal::reset() {
  rx_ = Rx::M0;
  got_ = 0;
  cur_x_ = cur_y_ = 0;
  cur_flags_ = 0;
  prev_cur_x_ = prev_cur_y_ = -1;
  prev_cur_flags_ = 0;
  linked_ = false;
  bye_ = false;
  last_good_ms_ = 0;
  invalidateCache();
  if (tft_)
    tft_->fillScreen(TFT_BLACK);
}

void Terminal::invalidateCache() {
  for (int y = 0; y < TERM_ROWS; y++) {
    for (int x = 0; x < TERM_COLS; x++) {
      cp_[y][x] = ' ';
      attr_[y][x] = 0x07;
      prev_cp_[y][x] = 0;
      prev_attr_[y][x] = 0xFF;
    }
    row_dirty_[y] = true;
  }
  cur_flags_ = 0;
  prev_cur_flags_ = 0xFF;
}

bool Terminal::takeBye() {
  if (!bye_)
    return false;
  bye_ = false;
  return true;
}

void Terminal::showBanner(const char *title, const char *subtitle, uint16_t accent) {
  if (!tft_)
    return;
  invalidateCache();
  linked_ = false;
  cur_flags_ = 0;

  tft_->fillScreen(TFT_BLACK);
  tft_->setTextDatum(TL_DATUM);
  tft_->setTextFont(1);
  tft_->setTextSize(1);
  tft_->setTextColor(accent, TFT_BLACK);
  tft_->drawString(title ? title : "", 2, 2, 1);
  tft_->setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft_->drawString(subtitle ? subtitle : "", 2, 2 + TERM_CELL_H, 1);
}

void Terminal::invalidateCells(int x0, int y0, int cols, int rows) {
  if (cols <= 0 || rows <= 0)
    return;
  int x1 = x0 + cols - 1;
  int y1 = y0 + rows - 1;
  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 >= TERM_COLS)
    x1 = TERM_COLS - 1;
  if (y1 >= TERM_ROWS)
    y1 = TERM_ROWS - 1;
  for (int y = y0; y <= y1; y++) {
    row_dirty_[y] = true;
    for (int x = x0; x <= x1; x++)
      prev_attr_[y][x] = 0xFF;
  }
}

void Terminal::setOverlayCells(int x0, int y0, int cols, int rows) {
  if (cols <= 0 || rows <= 0) {
    hole_on_ = false;
    return;
  }
  hole_x0_ = x0;
  hole_y0_ = y0;
  hole_x1_ = x0 + cols - 1;
  hole_y1_ = y0 + rows - 1;
  if (hole_x0_ < 0)
    hole_x0_ = 0;
  if (hole_y0_ < 0)
    hole_y0_ = 0;
  if (hole_x1_ >= TERM_COLS)
    hole_x1_ = TERM_COLS - 1;
  if (hole_y1_ >= TERM_ROWS)
    hole_y1_ = TERM_ROWS - 1;
  hole_on_ = true;
}

bool Terminal::cellInHole(int x, int y) const {
  return hole_on_ && x >= hole_x0_ && x <= hole_x1_ && y >= hole_y0_ &&
         y <= hole_y1_;
}

void Terminal::reply(uint8_t code, uint8_t seq) {
  Serial.write(code);
  Serial.write(seq);
}

uint16_t Terminal::ansiToRgb(uint8_t idx) const { return kAnsi16[idx & 0x0F]; }

void Terminal::blitRows(int px, int py, const uint8_t *rows, uint16_t fg,
                        uint16_t bg) {
  for (int y = 0; y < TERM_CELL_H; y++) {
    uint8_t bits = rows[y];
    for (int x = 0; x < TERM_CELL_W; x++) {
      bool on = bits & (1u << (TERM_CELL_W - 1 - x));
      tft_->drawPixel(px + x, py + y, on ? fg : bg);
    }
  }
}

bool Terminal::blitProcedural(int px, int py, uint16_t cp, uint16_t fg,
                              uint16_t bg) {
  uint8_t rows[TERM_CELL_H];
  if (!(glyphBox(cp, rows) || glyphBlock(cp, rows) || glyphBraille(cp, rows) ||
        glyphArrowEtc(cp, rows)))
    return false;
  blitRows(px, py, rows, fg, bg);
  return true;
}

void Terminal::paintCell(int x, int y, bool cursor_block) {
  uint16_t cp = cp_[y][x];
  uint8_t a = attr_[y][x];
  uint8_t fi = a & 0x0F;
  uint8_t bi = (a >> 4) & 0x0F;
  if (cursor_block) {
    uint8_t t = fi;
    fi = bi;
    bi = t;
    if (fi == bi)
      fi = fi ^ 0x07;
  }
  uint16_t fg = ansiToRgb(fi);
  uint16_t bg = ansiToRgb(bi);
  const int px = x * TERM_CELL_W;
  const int py = y * TERM_CELL_H;

  if (cp == 0 || cp == ' ' || cp == 0x00A0) {
    tft_->fillRect(px, py, TERM_CELL_W, TERM_CELL_H, bg);
    if (cursor_block)
      tft_->drawRect(px, py, TERM_CELL_W, TERM_CELL_H, fg);
    return;
  }

  if (cp >= 0x20 && cp <= 0x7E) {
    tft_->fillRect(px, py, TERM_CELL_W, TERM_CELL_H, bg);
    tft_->drawChar(px, py, (uint8_t)cp, fg, bg, 1);
    return;
  }

  if (blitProcedural(px, py, cp, fg, bg))
    return;

  tft_->fillRect(px, py, TERM_CELL_W, TERM_CELL_H, bg);
  tft_->drawChar(px, py, '?', fg, bg, 1);
}

void Terminal::flush() {
  if (!tft_)
    return;
  tft_->setTextFont(1);
  tft_->setTextSize(1);

  const bool cur_lit =
      (cur_flags_ & CURSOR_VISIBLE) && (cur_flags_ & CURSOR_ON) && cur_x_ >= 0 &&
      cur_y_ >= 0 && cur_x_ < TERM_COLS && cur_y_ < TERM_ROWS;

  if (cur_x_ != prev_cur_x_ || cur_y_ != prev_cur_y_ ||
      cur_flags_ != prev_cur_flags_) {
    if (prev_cur_y_ >= 0 && prev_cur_y_ < TERM_ROWS)
      row_dirty_[prev_cur_y_] = true;
    if (cur_y_ >= 0 && cur_y_ < TERM_ROWS)
      row_dirty_[cur_y_] = true;
    if (prev_cur_y_ >= 0 && prev_cur_x_ >= 0 && prev_cur_y_ < TERM_ROWS &&
        prev_cur_x_ < TERM_COLS)
      prev_attr_[prev_cur_y_][prev_cur_x_] = 0xFF;
    if (cur_lit)
      prev_attr_[cur_y_][cur_x_] = 0xFF;
  }

  for (int y = 0; y < TERM_ROWS; y++) {
    if (!row_dirty_[y])
      continue;
    for (int x = 0; x < TERM_COLS; x++) {
      const bool is_cur = cur_lit && x == cur_x_ && y == cur_y_;
      if (is_cur || cp_[y][x] != prev_cp_[y][x] ||
          attr_[y][x] != prev_attr_[y][x]) {
        if (!cellInHole(x, y))
          paintCell(x, y, is_cur);
        prev_cp_[y][x] = cp_[y][x];
        prev_attr_[y][x] = attr_[y][x];
      }
    }
    row_dirty_[y] = false;
  }

  prev_cur_x_ = cur_x_;
  prev_cur_y_ = cur_y_;
  prev_cur_flags_ = cur_flags_;
}

void Terminal::applyPayload() {
  const uint8_t *p = payload_;
  for (int y = 0; y < TERM_ROWS; y++) {
    bool dirty = false;
    for (int x = 0; x < TERM_COLS; x++) {
      uint16_t cp = ((uint16_t)p[0] << 8) | p[1];
      uint8_t a = p[2];
      p += 3;
      if (cp != cp_[y][x] || a != attr_[y][x]) {
        cp_[y][x] = cp;
        attr_[y][x] = a;
        dirty = true;
      }
    }
    if (dirty)
      row_dirty_[y] = true;
  }
  cur_x_ = (int8_t)rx_cx_;
  cur_y_ = (int8_t)rx_cy_;
  cur_flags_ = rx_flags_ & (uint8_t)~FLAG_BYE;
  last_good_ms_ = millis();
  if (rx_flags_ & FLAG_BYE) {
    bye_ = true;
    linked_ = false;
    cur_flags_ = 0;
  } else {
    linked_ = true;
  }
}

void Terminal::ingest(const uint8_t *data, size_t n) {
  for (size_t i = 0; i < n; i++) {
    uint8_t b = data[i];
    switch (rx_) {
    case Rx::M0:
      rx_ = (b == FRAME_M0) ? Rx::M1 : Rx::M0;
      break;
    case Rx::M1:
      rx_ = (b == FRAME_M1) ? Rx::M2 : (b == FRAME_M0) ? Rx::M1 : Rx::M0;
      break;
    case Rx::M2:
      rx_ = (b == FRAME_M2) ? Rx::M3 : (b == FRAME_M0) ? Rx::M1 : Rx::M0;
      break;
    case Rx::M3:
      rx_ = (b == FRAME_M3) ? Rx::Seq : (b == FRAME_M0) ? Rx::M1 : Rx::M0;
      break;
    case Rx::Seq:
      seq_ = b;
      rx_ = Rx::Cx;
      break;
    case Rx::Cx:
      rx_cx_ = b;
      rx_ = Rx::Cy;
      break;
    case Rx::Cy:
      rx_cy_ = b;
      rx_ = Rx::Flags;
      break;
    case Rx::Flags:
      rx_flags_ = b;
      got_ = 0;
      rx_ = Rx::Payload;
      break;
    case Rx::Payload:
      payload_[got_++] = b;
      if (got_ >= TERM_PAYLOAD)
        rx_ = Rx::CrcHi;
      break;
    case Rx::CrcHi:
      crc_expect_ = (uint16_t)b << 8;
      rx_ = Rx::CrcLo;
      break;
    case Rx::CrcLo: {
      crc_expect_ |= b;
      uint8_t hdr[4] = {seq_, rx_cx_, rx_cy_, rx_flags_};
      uint16_t crc = crc16(hdr, 4);
      crc = crc16(payload_, TERM_PAYLOAD, crc);
      if (crc == crc_expect_) {
        applyPayload();
        reply(FRAME_ACK, seq_);
      } else {
        reply(FRAME_NAK, seq_);
      }
      rx_ = Rx::M0;
      break;
    }
    }
  }
}
