#include "osd.h"

#include <Preferences.h>

#include "protocol.h"
#include "status_ui.h"

#define RGB565(r, g, b)                                                        \
  (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

namespace osd {
namespace {

#ifndef TTY_BUDDY_VERSION
#define TTY_BUDDY_VERSION "0.0.0"
#endif
constexpr const char *FW_STRING = "FW v" TTY_BUDDY_VERSION;
constexpr uint16_t COL_FW = TFT_YELLOW;

constexpr int PIN_BL = 5;
constexpr int PIN_BTN = 10;
constexpr int BL_PWM_CH = 0;
constexpr int BL_PWM_FREQ = 5000;
constexpr int BL_PWM_BITS = 8;

constexpr uint32_t BTN_DEBOUNCE_MS = 40;
constexpr uint32_t BTN_LONG_MS = 700;
constexpr uint32_t IDLE_CLOSE_MS = 8000;

// Config levels 1..6 → duty (off is sleep, not a bright step).
constexpr uint8_t BRIGHT_STEPS = 6;
constexpr uint8_t BRIGHT_DUTY[BRIGHT_STEPS] = {42, 85, 128, 170, 213, 255};
constexpr uint8_t BRIGHT_PCT[BRIGHT_STEPS] = {16, 33, 50, 67, 84, 100};

// Config levels: 0 = never, 1..6 → seconds.
constexpr uint8_t SLEEP_STEPS = 6;
constexpr uint16_t SLEEP_SECS[SLEEP_STEPS + 1] = {
    0, 30, 60, 120, 300, 600, 1800};

enum Item : uint8_t { ITEM_MODE = 0, ITEM_BRIGHT, ITEM_SLEEP, ITEM_COUNT };

constexpr int PAD_X = 6;
constexpr int PAD_Y = 4;
constexpr int ROW_H = TERM_CELL_H + 2; // tighter than a blank cell row
constexpr int OSD_COLS = 22;
constexpr int PANEL_INNER_H = PAD_Y * 2 + ITEM_COUNT * ROW_H + TERM_CELL_H;
constexpr int OSD_ROWS = (PANEL_INNER_H + TERM_CELL_H - 1) / TERM_CELL_H;
constexpr int CELL_X0 = TERM_COLS - OSD_COLS;
constexpr int CELL_Y0 = 0;
constexpr int PX_X0 = CELL_X0 * TERM_CELL_W;
constexpr int PX_Y0 = 0;
constexpr int PANEL_W = 320 - PX_X0;
constexpr int PANEL_H = OSD_ROWS * TERM_CELL_H;

constexpr uint16_t COL_BG = RGB565(0x00, 0x00, 0x40);
constexpr uint16_t COL_BORDER = TFT_CYAN;
constexpr uint16_t COL_LABEL = RGB565(0xAA, 0xAA, 0xAA);
constexpr uint16_t COL_SEL = TFT_WHITE;
constexpr uint16_t COL_VAL = TFT_CYAN;

TFT_eSPI *g_tft = nullptr;
Terminal *g_term = nullptr;

bool g_open = false;
uint8_t g_item = 0;
uint32_t g_last_activity_ms = 0;

bool g_asleep = false;
bool g_wake_consumed = false;
bool g_alert_was_up = false;

uint8_t g_bright_step = 4;
bool g_auto_bright = false;
uint8_t g_sleep_level = 4;
bool g_was_status = false;
uint8_t g_host_bright = 0xFF;
uint8_t g_host_sleep = 0xFF;
uint32_t g_ignore_host_osd_until = 0;
Preferences g_prefs;

bool g_dismiss_on_tap_cfg = true;
bool g_wake_on_alert_cfg = true;
uint8_t g_auto_day_step = 5;
uint8_t g_auto_night_step = 2;
uint8_t g_auto_day_hour = 7;
uint8_t g_auto_night_hour = 21;

bool btn_stable = true;
bool btn_raw = true;
uint32_t btn_change_ms = 0;
uint32_t btn_down_ms = 0;
bool btn_long_sent = false;

bool g_need_chrome = false;
bool g_row_dirty[ITEM_COUNT]{};
bool g_fw_dirty = false;
uint8_t g_painted_item = 0xFF;
char g_painted_val[ITEM_COUNT][12]{};

uint8_t clampStep(uint8_t v, uint8_t def) {
  if (v < 1 || v > BRIGHT_STEPS)
    return def;
  return v;
}

uint8_t clampSleep(uint8_t v) {
  return v > SLEEP_STEPS ? SLEEP_STEPS : v;
}

uint8_t dutyForStep(uint8_t step) {
  return BRIGHT_DUTY[clampStep(step, 4) - 1];
}

void applyBacklight() {
  if (g_asleep) {
    ledcWrite(BL_PWM_CH, 0);
    return;
  }
  ledcWrite(BL_PWM_CH, dutyForStep(g_bright_step));
}

int8_t parseHour(const char *t) {
  if (!t || t[0] < '0' || t[0] > '9' || t[1] < '0' || t[1] > '9')
    return -1;
  return (int8_t)((t[0] - '0') * 10 + (t[1] - '0'));
}

uint8_t computeAutoStep() {
  const int8_t hour = parseHour(g_term->statusSnap().time);
  if (hour < 0)
    return g_auto_day_step;
  bool is_day;
  if (g_auto_day_hour == g_auto_night_hour)
    is_day = true;
  else if (g_auto_day_hour < g_auto_night_hour)
    is_day = hour >= g_auto_day_hour && hour < g_auto_night_hour;
  else
    is_day = hour >= g_auto_day_hour || hour < g_auto_night_hour;
  return is_day ? g_auto_day_step : g_auto_night_step;
}

void recomputeAutoBrightness() {
  if (!g_auto_bright)
    return;
  const uint8_t step = computeAutoStep();
  if (step != g_bright_step) {
    g_bright_step = step;
    applyBacklight();
    if (g_open)
      g_row_dirty[ITEM_BRIGHT] = true;
  }
}

void persistOsd() {
  if (!g_prefs.begin("osd", false))
    return;
  g_prefs.putUChar("bright", g_auto_bright ? 0 : g_bright_step);
  g_prefs.putUChar("sleep", g_sleep_level);
  g_prefs.end();
}

void loadOsdFromEeprom() {
  if (!g_prefs.begin("osd", true))
    return;
  if (g_prefs.isKey("bright")) {
    uint8_t b = g_prefs.getUChar("bright", 4);
    if (b == 0) {
      g_auto_bright = true;
      g_bright_step = computeAutoStep();
    } else {
      g_auto_bright = false;
      g_bright_step = clampStep(b, 4);
    }
    g_host_bright = b;
  }
  if (g_prefs.isKey("sleep")) {
    g_sleep_level = clampSleep(g_prefs.getUChar("sleep", 4));
    g_host_sleep = g_sleep_level;
  }
  g_prefs.end();
  applyBacklight();
}

void reportOsdBright() {
  Serial.write(DEV_OSD_BRIGHT);
  Serial.write(g_auto_bright ? 0 : g_bright_step);
}

void reportOsdSleep() {
  Serial.write(DEV_OSD_SLEEP);
  Serial.write(g_sleep_level);
}

void applyBrightFromHost(uint8_t b) {
  auto mapBright = [](uint8_t v, uint8_t def) -> uint8_t {
    if (v >= 1 && v <= BRIGHT_STEPS)
      return v;
    if (v > BRIGHT_STEPS)
      return clampStep((uint8_t)(((uint16_t)v * BRIGHT_STEPS + 50) / 100), def);
    return def;
  };
  if (b == 0) {
    g_auto_bright = true;
    g_bright_step = computeAutoStep();
  } else {
    g_auto_bright = false;
    g_bright_step = mapBright(b, 4);
  }
  g_host_bright = b;
  applyBacklight();
  persistOsd();
  if (g_open)
    g_row_dirty[ITEM_BRIGHT] = true;
}

void applySleepFromHost(uint16_t sl) {
  if (sl <= SLEEP_STEPS) {
    g_sleep_level = (uint8_t)sl;
  } else {
    g_sleep_level = SLEEP_STEPS;
    for (uint8_t i = 0; i <= SLEEP_STEPS; i++) {
      if (SLEEP_SECS[i] >= sl) {
        g_sleep_level = i;
        break;
      }
    }
  }
  g_host_sleep = g_sleep_level;
  persistOsd();
  if (g_open)
    g_row_dirty[ITEM_SLEEP] = true;
}

void syncFromStyle() {
  if (!g_term->hasStatusSnap())
    return;
  const StatusStyle &st = g_term->statusSnap().style;

  g_dismiss_on_tap_cfg = (st.osd_flags & OSD_F_DISMISS_ON_TAP) != 0;
  g_wake_on_alert_cfg = (st.osd_flags & OSD_F_WAKE_ON_ALERT) != 0;
  g_auto_day_hour = st.osd_auto_day_hour;
  g_auto_night_hour = st.osd_auto_night_hour;

  auto mapBright = [](uint8_t v, uint8_t def) -> uint8_t {
    if (v >= 1 && v <= BRIGHT_STEPS)
      return v;
    if (v > BRIGHT_STEPS)
      return clampStep((uint8_t)(((uint16_t)v * BRIGHT_STEPS + 50) / 100), def);
    return def;
  };
  g_auto_day_step = mapBright(st.osd_auto_day_pct, 5);
  g_auto_night_step = mapBright(st.osd_auto_night_pct, 2);

  // Host style is SoT once linked; NVS applies until the first style/status frame.
  if (millis() < g_ignore_host_osd_until)
    return;

  uint8_t b = st.osd_default_bright_pct;
  if ((st.osd_flags & OSD_F_AUTO_BRIGHT) != 0)
    b = 0;
  uint8_t sleep_lv;
  {
    uint16_t sl = st.osd_sleep_timeout_s;
    if (sl <= SLEEP_STEPS) {
      sleep_lv = (uint8_t)sl;
    } else {
      sleep_lv = SLEEP_STEPS;
      for (uint8_t i = 0; i <= SLEEP_STEPS; i++) {
        if (SLEEP_SECS[i] >= sl) {
          sleep_lv = i;
          break;
        }
      }
    }
  }

  if (b != g_host_bright)
    applyBrightFromHost(b);
  if (sleep_lv != g_host_sleep)
    applySleepFromHost(st.osd_sleep_timeout_s);
}

void cycleBrightness() {
  if (g_auto_bright) {
    g_auto_bright = false;
    g_bright_step = 1;
  } else if (g_bright_step >= BRIGHT_STEPS) {
    g_auto_bright = true;
    g_bright_step = computeAutoStep();
  } else {
    g_bright_step++;
  }
  g_host_bright = g_auto_bright ? 0 : g_bright_step;
  g_ignore_host_osd_until = millis() + 2500;
  applyBacklight();
  persistOsd();
  reportOsdBright();
}

void cycleSleep() {
  g_sleep_level = (uint8_t)((g_sleep_level + 1) % (SLEEP_STEPS + 1));
  g_host_sleep = g_sleep_level;
  g_ignore_host_osd_until = millis() + 2500;
  persistOsd();
  reportOsdSleep();
}

const char *itemLabel(uint8_t item) {
  switch (item) {
  case ITEM_MODE:
    return "Mode";
  case ITEM_BRIGHT:
    return "Brightness";
  default:
    return "Sleep";
  }
}

void fmtSleep(char *buf, size_t n, uint8_t level) {
  const uint16_t sec = SLEEP_SECS[clampSleep(level)];
  if (sec == 0)
    snprintf(buf, n, "NEVER");
  else if (sec < 60)
    snprintf(buf, n, "%uS", (unsigned)sec);
  else
    snprintf(buf, n, "%uM", (unsigned)(sec / 60));
}

void itemValue(uint8_t item, char *buf, size_t n) {
  switch (item) {
  case ITEM_MODE:
    snprintf(buf, n, "%s", g_term->statusUiActive() ? "STATUS" : "TERMINAL");
    break;
  case ITEM_BRIGHT:
    if (g_auto_bright)
      snprintf(buf, n, "AUTO");
    else
      snprintf(buf, n, "%u%%",
               (unsigned)BRIGHT_PCT[clampStep(g_bright_step, 4) - 1]);
    break;
  default:
    fmtSleep(buf, n, g_sleep_level);
    break;
  }
}

void markAllDirty() {
  g_need_chrome = true;
  g_fw_dirty = true;
  g_painted_item = 0xFF;
  for (uint8_t i = 0; i < ITEM_COUNT; i++) {
    g_row_dirty[i] = true;
    g_painted_val[i][0] = 0;
  }
}

void setOverlayHole(bool on) {
  if (on) {
    g_term->setOverlayCells(CELL_X0, CELL_Y0, OSD_COLS, OSD_ROWS);
    statusGuiSetOverlay(PX_X0, PX_Y0, PANEL_W, PANEL_H);
  } else {
    g_term->setOverlayCells(0, 0, 0, 0);
    statusGuiSetOverlay(0, 0, 0, 0);
  }
}

int rowY(uint8_t item) { return PX_Y0 + PAD_Y + (int)item * ROW_H; }

void paintChrome() {
  g_tft->fillRect(PX_X0, PX_Y0, PANEL_W, PANEL_H, COL_BG);
  g_tft->drawRect(PX_X0, PX_Y0, PANEL_W, PANEL_H, COL_BORDER);
  g_need_chrome = false;
  g_fw_dirty = true;
  for (uint8_t i = 0; i < ITEM_COUNT; i++)
    g_row_dirty[i] = true;
}

void paintRow(uint8_t item) {
  TFT_eSPI *tft = g_tft;
  const int y = rowY(item);
  const bool sel = (item == g_item);
  char val[12];
  itemValue(item, val, sizeof(val));

  tft->fillRect(PX_X0 + 1, y, PANEL_W - 2, TERM_CELL_H, COL_BG);
  tft->setTextDatum(TL_DATUM);
  tft->setTextFont(1);
  tft->setTextColor(sel ? COL_SEL : COL_LABEL, COL_BG);
  char left[16];
  snprintf(left, sizeof(left), "%c%s", sel ? '>' : ' ', itemLabel(item));
  tft->drawString(left, PX_X0 + PAD_X, y, 1);

  tft->setTextDatum(TR_DATUM);
  tft->setTextColor(sel ? COL_SEL : COL_VAL, COL_BG);
  tft->drawString(val, PX_X0 + PANEL_W - PAD_X, y, 1);
  tft->setTextDatum(TL_DATUM);

  strncpy(g_painted_val[item], val, sizeof(g_painted_val[item]) - 1);
  g_painted_val[item][sizeof(g_painted_val[item]) - 1] = 0;
  g_row_dirty[item] = false;
}

void paintFw() {
  const int y = rowY(ITEM_COUNT);
  // Match non-selected labels (skip the `>` column).
  const int x = PX_X0 + PAD_X + TERM_CELL_W;
  g_tft->fillRect(PX_X0 + 1, y, PANEL_W - 2, TERM_CELL_H, COL_BG);
  g_tft->setTextDatum(TL_DATUM);
  g_tft->setTextFont(1);
  g_tft->setTextColor(COL_FW, COL_BG);
  g_tft->drawString(FW_STRING, x, y, 1);
  g_fw_dirty = false;
}

void paintDirty() {
  if (!g_open || !g_tft)
    return;

  const bool status = g_term->statusUiActive();
  if (status != g_was_status) {
    // Mode switch can wipe the panel; re-hole and full dirty paint.
    g_was_status = status;
    setOverlayHole(true);
    markAllDirty();
  }

  if (g_need_chrome)
    paintChrome();

  if (g_painted_item != g_item && g_painted_item < ITEM_COUNT)
    g_row_dirty[g_painted_item] = true;
  if (g_painted_item != g_item)
    g_row_dirty[g_item] = true;

  for (uint8_t i = 0; i < ITEM_COUNT; i++) {
    if (!g_row_dirty[i]) {
      char cur[12];
      itemValue(i, cur, sizeof(cur));
      if (strncmp(cur, g_painted_val[i], sizeof(g_painted_val[i])) != 0)
        g_row_dirty[i] = true;
    }
    if (g_row_dirty[i])
      paintRow(i);
  }
  g_painted_item = g_item;

  if (g_fw_dirty)
    paintFw();
}

void closeOsd() {
  if (!g_open)
    return;
  g_open = false;
  setOverlayHole(false);
  if (g_term->statusUiActive()) {
    statusGuiRestoreRegion(g_tft, g_term->statusSnap(), PX_X0, PX_Y0, PANEL_W,
                           PANEL_H);
  } else {
    // Hole updated prev_* without painting; black fill + invalidate restores cells.
    g_tft->fillRect(PX_X0, PX_Y0, PANEL_W, PANEL_H, TFT_BLACK);
    g_term->invalidateCells(CELL_X0, CELL_Y0, OSD_COLS, OSD_ROWS);
  }
}

void openOsd(uint32_t now) {
  g_open = true;
  g_item = 0;
  g_last_activity_ms = now;
  g_was_status = g_term->statusUiActive();
  setOverlayHole(true);
  markAllDirty();
  paintDirty();
}

void enterSleep() {
  if (g_open)
    closeOsd();
  g_asleep = true;
  applyBacklight();
}

void wake(uint32_t now) {
  g_asleep = false;
  g_last_activity_ms = now;
  applyBacklight();
}

void handleShortPress(uint32_t now) {
  if (g_open) {
    if (g_item + 1 >= ITEM_COUNT) {
      closeOsd();
    } else {
      g_item++;
      g_last_activity_ms = now;
      paintDirty();
    }
    return;
  }
  const bool alert_up = g_term->statusUiActive() && statusGuiAlertActive();
  if (alert_up && g_dismiss_on_tap_cfg) {
    statusGuiDismissAlert();
    g_term->requestStatusRepaint();
    return;
  }
  openOsd(now);
}

void handleLongPress(uint32_t now) {
  if (!g_open) {
    // Idle long-press: silent mode toggle (no OSD).
    Serial.write(DEV_MODE_TOGGLE);
    g_last_activity_ms = now;
    return;
  }
  switch (g_item) {
  case ITEM_MODE:
    Serial.write(DEV_MODE_TOGGLE);
    break;
  case ITEM_BRIGHT:
    cycleBrightness();
    break;
  case ITEM_SLEEP:
    cycleSleep();
    break;
  }
  g_last_activity_ms = now;
  g_row_dirty[g_item] = true;
  paintDirty();
}

void pollButton(uint32_t now) {
  const bool raw = digitalRead(PIN_BTN);
  if (raw != btn_raw) {
    btn_raw = raw;
    btn_change_ms = now;
  }

  if (now - btn_change_ms >= BTN_DEBOUNCE_MS && raw != btn_stable) {
    const bool was_high = btn_stable;
    btn_stable = raw;

    if (was_high && !btn_stable) {
      btn_down_ms = now;
      btn_long_sent = false;
      if (g_asleep) {
        wake(now);
        g_wake_consumed = true;
      } else {
        g_last_activity_ms = now;
      }
    } else if (!was_high && btn_stable) {
      if (g_wake_consumed)
        g_wake_consumed = false;
      else if (!btn_long_sent)
        handleShortPress(now);
    }
  }

  if (!btn_stable && !btn_long_sent && now - btn_down_ms >= BTN_LONG_MS) {
    btn_long_sent = true;
    if (!g_wake_consumed)
      handleLongPress(now);
  }
}

} // namespace

void begin(TFT_eSPI *tft, Terminal *term) {
  g_tft = tft;
  g_term = term;

  pinMode(PIN_BTN, INPUT_PULLUP);
  ledcSetup(BL_PWM_CH, BL_PWM_FREQ, BL_PWM_BITS);
  ledcAttachPin(PIN_BL, BL_PWM_CH);
  loadOsdFromEeprom();
  applyBacklight();
}

void tick(uint32_t now) {
  pollButton(now);
  syncFromStyle();
  recomputeAutoBrightness();

  // Wake (and hold awake) while a status alert is showing.
  const bool alert_up = g_term->statusUiActive() && statusGuiAlertActive();
  if (g_wake_on_alert_cfg && alert_up) {
    if (g_asleep || !g_alert_was_up)
      wake(now);
    else
      g_last_activity_ms = now;
  }
  g_alert_was_up = alert_up;

  if (g_open && now - g_last_activity_ms >= IDLE_CLOSE_MS)
    closeOsd();

  const uint16_t sleep_s = SLEEP_SECS[clampSleep(g_sleep_level)];
  if (!g_asleep && !g_open && sleep_s > 0 &&
      now - g_last_activity_ms >= (uint32_t)sleep_s * 1000u)
    enterSleep();
}

void paint() { paintDirty(); }

void noteActivity(uint32_t now) {
  if (g_asleep)
    wake(now);
  else
    g_last_activity_ms = now;
}

bool isOpen() { return g_open; }
bool isAsleep() { return g_asleep; }

} // namespace osd
