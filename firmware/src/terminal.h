#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "protocol.h"
#include "status_ui.h"

// Font 1 (GLCD) 6×8 ASCII + on-device procedural Unicode (box/block/Braille).

class Terminal {
public:
  void begin(TFT_eSPI *display);
  void reset();
  void ingest(const uint8_t *data, size_t n);
  void flush();

  void showBanner(const char *title, const char *subtitle, uint16_t accent);
  void invalidateCells(int x0, int y0, int cols, int rows);
  void setOverlayCells(int x0, int y0, int cols, int rows);

  bool linked() const { return linked_; }
  bool takeBye();
  uint32_t lastGoodFrameMs() const { return last_good_ms_; }
  bool statusUiActive() const { return status_ui_; }
  const StatusSnap &statusSnap() const { return status_; }
  /// Distinct from statusUiActive(): survives mode switches until a new snap.
  bool hasStatusSnap() const { return status_seen_; }
  /// Mark status UI dirty (optionally force a full paint).
  void requestStatusRepaint(bool full = false) {
    status_dirty_ = true;
    if (full)
      status_full_paint_ = true;
  }
  void paintStatusUi();

  int cols() const { return TERM_COLS; }
  int rows() const { return TERM_ROWS; }

private:
  enum class Rx : uint8_t {
    M0,
    M1,
    M2,
    M3,
    Seq,
    Cx,
    Cy,
    Flags,
    Payload,
    CrcHi,
    CrcLo
  };

  TFT_eSPI *tft_ = nullptr;
  uint16_t cp_[TERM_ROWS][TERM_COLS]{};
  uint8_t attr_[TERM_ROWS][TERM_COLS]{};
  uint16_t prev_cp_[TERM_ROWS][TERM_COLS]{};
  uint8_t prev_attr_[TERM_ROWS][TERM_COLS]{};
  bool row_dirty_[TERM_ROWS]{};

  StatusSnap status_{};
  bool status_ui_ = false;
  bool status_dirty_ = false;
  bool status_full_paint_ = false;

  int8_t cur_x_ = 0, cur_y_ = 0;
  uint8_t cur_flags_ = 0;
  int8_t prev_cur_x_ = -1, prev_cur_y_ = -1;
  uint8_t prev_cur_flags_ = 0;

  bool linked_ = false;
  bool status_seen_ = false;
  bool bye_ = false;
  uint32_t last_good_ms_ = 0;

  Rx rx_ = Rx::M0;
  uint8_t seq_ = 0;
  uint8_t rx_cx_ = 0, rx_cy_ = 0, rx_flags_ = 0;
  uint16_t got_ = 0;
  uint8_t payload_[TERM_PAYLOAD]{};
  uint16_t crc_expect_ = 0;

  bool hole_on_ = false;
  int hole_x0_ = 0, hole_y0_ = 0, hole_x1_ = 0, hole_y1_ = 0;

  void applyPayload();
  void paintCell(int x, int y, bool cursor_block);
  bool blitProcedural(int px, int py, uint16_t cp, uint16_t fg, uint16_t bg);
  void blitRows(int px, int py, const uint8_t *rows, uint16_t fg, uint16_t bg);
  void invalidateCache();
  bool cellInHole(int x, int y) const;
  uint16_t ansiToRgb(uint8_t idx) const;
  void reply(uint8_t code, uint8_t seq);
};
