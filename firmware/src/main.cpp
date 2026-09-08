#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>

#include "terminal.h"

static constexpr uint32_t LINK_TIMEOUT_MS = 12000;

static constexpr int PIN_BL = 5;
static constexpr int PIN_BTN = 10;
static constexpr int BL_PWM_CH = 0;
static constexpr int BL_PWM_FREQ = 5000;
static constexpr int BL_PWM_BITS = 8;

static constexpr int BL_STEPS = 5;
static constexpr uint8_t BL_DUTY[BL_STEPS] = {51, 102, 153, 204, 255}; // ~20–100%

static constexpr uint32_t BTN_DEBOUNCE_MS = 40;
static constexpr uint32_t BTN_LONG_MS = 700;

TFT_eSPI tft;
Terminal term;

enum class LinkUI : uint8_t { Waiting, Live, Lost };

static LinkUI link_ui = LinkUI::Waiting;
static uint32_t last_flush_ms = 0;

static int bl_step = BL_STEPS - 1;

static bool btn_stable = true; // INPUT_PULLUP idle
static bool btn_raw = true;
static uint32_t btn_change_ms = 0;
static uint32_t btn_down_ms = 0;
static bool btn_long_sent = false;

static void applyBacklight() {
  ledcWrite(BL_PWM_CH, BL_DUTY[bl_step]);
}

static void paintWaiting() {
  term.showBanner("Waiting for daemon", "systemctl start tty-buddy", TFT_CYAN);
}

static void paintLost() {
  term.showBanner("Lost connection to daemon", "waiting to reconnect...",
                  TFT_ORANGE);
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
    btn_down_ms = now;
    btn_long_sent = false;
    return;
  }

  if (!was_high && btn_stable) {
    if (btn_long_sent)
      return;
    // Backlight only — brightness OSD was removed (left a top-strip hole).
    bl_step = (bl_step + 1) % BL_STEPS;
    applyBacklight();
    return;
  }
}

static void pollLongPress(uint32_t now) {
  if (btn_stable || btn_long_sent)
    return;
  if (now - btn_down_ms < BTN_LONG_MS)
    return;
  btn_long_sent = true;
  Serial.write(DEV_MODE_TOGGLE);
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
  pollLongPress(now);

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
  } else if (alive) {
    link_ui = LinkUI::Live;
  }

  if (link_ui == LinkUI::Live && now - last_flush_ms >= 16) {
    last_flush_ms = now;
    term.flush();
  }
}
