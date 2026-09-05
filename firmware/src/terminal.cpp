#include "terminal.h"

#include <string.h>

static const uint16_t kAnsi16[16] = {
    0x0000, 0xA800, 0x0540, 0xAB40, 0x0015, 0xA815, 0x0555, 0xAD55,
    0x52AA, 0xF800, 0x07E0, 0xFFE0, 0x001F, 0xF81F, 0x07FF, 0xFFFF,
};

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
      ch_[y][x] = ' ';
      attr_[y][x] = 0x07;
      prev_ch_[y][x] = 0;
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

void Terminal::paintCell(int x, int y, bool cursor_block) {
  uint8_t c = ch_[y][x];
  if (c < 0x20 || c > 0x7E)
    c = ' ';
  uint8_t a = attr_[y][x];
  uint8_t fi = a & 0x0F;
  uint8_t bi = (a >> 4) & 0x0F;
  if (cursor_block) {
    uint8_t t = fi;
    fi = bi;
    bi = t;
    // Invert alone is invisible when fg == bg (e.g. black-on-black).
    if (fi == bi)
      fi = fi ^ 0x07;
  }
  uint16_t fg = ansiToRgb(fi);
  uint16_t bg = ansiToRgb(bi);
  const int px = x * TERM_CELL_W;
  const int py = y * TERM_CELL_H;
  tft_->fillRect(px, py, TERM_CELL_W, TERM_CELL_H, bg);
  if (c != ' ')
    tft_->drawChar(px, py, c, fg, bg, 1);
  else if (cursor_block) {
    // Blank cell: outline so the cursor still shows.
    tft_->drawRect(px, py, TERM_CELL_W, TERM_CELL_H, fg);
  }
}

void Terminal::flush() {
  if (!tft_)
    return;
  tft_->setTextFont(1);
  tft_->setTextSize(1);

  const bool cur_lit =
      (cur_flags_ & CURSOR_VISIBLE) && (cur_flags_ & CURSOR_ON) && cur_x_ >= 0 &&
      cur_y_ >= 0 && cur_x_ < TERM_COLS && cur_y_ < TERM_ROWS;

  // Cursor move/blink does not change ch/attr — force those cells dirty.
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
      if (is_cur || ch_[y][x] != prev_ch_[y][x] ||
          attr_[y][x] != prev_attr_[y][x]) {
        if (!cellInHole(x, y))
          paintCell(x, y, is_cur);
        prev_ch_[y][x] = ch_[y][x];
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
      uint8_t c = *p++;
      uint8_t a = *p++;
      if (c < 0x20 || c > 0x7E)
        c = ' ';
      if (c != ch_[y][x] || a != attr_[y][x]) {
        ch_[y][x] = c;
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
