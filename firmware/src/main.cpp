#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>

#include "terminal.h"

static constexpr uint32_t LINK_TIMEOUT_MS = 2000;

static constexpr int PIN_BL = 5;
static constexpr int PIN_BTN = 10;
static constexpr int BL_PWM_CH = 0;
static constexpr int BL_PWM_FREQ = 5000;
static constexpr int BL_PWM_BITS = 8;

static constexpr int BL_STEPS = 5;
static constexpr uint8_t BL_DUTY[BL_STEPS] = {51, 102, 153, 204, 255}; // ~20–100%

static constexpr uint32_t BTN_DEBOUNCE_MS = 40;
static constexpr uint32_t OSD_MS = 3000;

// Top-right OSD: cell-aligned hole, drawn flush to the panel edges.
static constexpr int OSD_COLS = 15;
static constexpr int OSD_ROWS = 3;
static constexpr int OSD_CELL_X = TERM_COLS - OSD_COLS;
static constexpr int OSD_CELL_Y = 0;
static constexpr int OSD_X = OSD_CELL_X * TERM_CELL_W;
static constexpr int OSD_Y = 0;
static constexpr int OSD_W = 320 - OSD_X; // 53*6=318; extra 2px cover the right gutter
static constexpr int OSD_H = OSD_ROWS * TERM_CELL_H;

TFT_eSPI tft;
Terminal term;

enum class LinkUI : uint8_t { Waiting, Live, Lost };

static LinkUI link_ui = LinkUI::Waiting;
static uint32_t last_flush_ms = 0;

static int bl_step = BL_STEPS - 1;
static bool osd_visible = false;
static uint32_t osd_until_ms = 0;

static bool btn_stable = true; // INPUT_PULLUP idle
static bool btn_raw = true;
static uint32_t btn_change_ms = 0;

static void applyBacklight() {
  ledcWrite(BL_PWM_CH, BL_DUTY[bl_step]);
}

static void paintWaiting() {
  term.showBanner("Waiting for daemon", "run: tools/tty_bridge.py", TFT_CYAN);
}

static void paintLost() {
  term.showBanner("Lost connection to daemon", "waiting to reconnect...",
                  TFT_ORANGE);
}

static void paintOsd() {
  char line[24];
  snprintf(line, sizeof(line), "BRIGHT  %d/%d", bl_step + 1, BL_STEPS);

  tft.fillRect(OSD_X, OSD_Y, OSD_W, OSD_H, TFT_NAVY);
  tft.drawRect(OSD_X, OSD_Y, OSD_W, OSD_H, TFT_CYAN);

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.drawString(line, OSD_X + 4, OSD_Y + 3, 1);

  const int bar_x = OSD_X + 4;
  const int bar_y = OSD_Y + 14;
  const int bar_w = OSD_W - 8;
  const int bar_h = 6;
  tft.fillRect(bar_x, bar_y, bar_w, bar_h, TFT_DARKGREY);
  const int fill = (bar_w * (bl_step + 1)) / BL_STEPS;
  tft.fillRect(bar_x, bar_y, fill, bar_h, TFT_CYAN);
}

static void hideOsd() {
  if (!osd_visible)
    return;
  osd_visible = false;
  term.setOverlayCells(0, 0, 0, 0);
  // Cell flush cannot clear the 2px gutter past col 52.
  tft.fillRect(OSD_X, OSD_Y, OSD_W, OSD_H, TFT_BLACK);
  term.invalidateCells(OSD_CELL_X, OSD_CELL_Y, OSD_COLS, OSD_ROWS);
  if (link_ui == LinkUI::Live) {
    term.flush();
  } else if (link_ui == LinkUI::Waiting) {
    paintWaiting();
  } else {
    paintLost();
  }
}

static void showBrightnessOsd() {
  osd_visible = true;
  osd_until_ms = millis() + OSD_MS;
  term.setOverlayCells(OSD_CELL_X, OSD_CELL_Y, OSD_COLS, OSD_ROWS);
  paintOsd();
}

static void pollButton(uint32_t now) {
  const bool raw = digitalRead(PIN_BTN);
  if (raw != btn_raw) {
    btn_raw = raw;
    btn_change_ms = now;
  }
  if (now - btn_change_ms < BTN_DEBOUNCE_MS || raw == btn_stable)
    return;

  const bool was_high = btn_stable;
  btn_stable = raw;
  if (was_high && !btn_stable) {
    bl_step = (bl_step + 1) % BL_STEPS;
    applyBacklight();
    showBrightnessOsd();
  }
}

void setup() {
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_BTN, INPUT_PULLUP);
  ledcSetup(BL_PWM_CH, BL_PWM_FREQ, BL_PWM_BITS);
  ledcAttachPin(PIN_BL, BL_PWM_CH);
  applyBacklight();

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  term.begin(&tft);
  paintWaiting();
  link_ui = LinkUI::Waiting;
}

void loop() {
  uint32_t now = millis();
  pollButton(now);

  if (osd_visible && (int32_t)(now - osd_until_ms) >= 0)
    hideOsd();

  for (;;) {
    int avail = Serial.available();
    if (avail <= 0)
      break;
    uint8_t buf[1024];
    int n = avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail;
    int got = Serial.readBytes(buf, n);
    if (got > 0)
      term.ingest(buf, (size_t)got);
  }

  // lastGoodFrameMs() is stamped during ingest — must not compare against a
  // pre-ingest millis() (unsigned underflow looks like a link timeout).
  now = millis();
  const bool bye = term.takeBye();
  const bool alive =
      term.linked() && (now - term.lastGoodFrameMs() <= LINK_TIMEOUT_MS);

  if (bye || (link_ui == LinkUI::Live && !alive)) {
    paintLost();
    link_ui = LinkUI::Lost;
    if (osd_visible)
      paintOsd();
  } else if (alive) {
    link_ui = LinkUI::Live;
  }

  if (link_ui == LinkUI::Live && now - last_flush_ms >= 16) {
    last_flush_ms = now;
    term.flush();
  }
}
