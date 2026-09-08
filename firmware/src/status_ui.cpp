#include "status_ui.h"

#include <stdio.h>
#include <string.h>

#define RGB565(r, g, b)                                                        \
  (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

static constexpr uint16_t COL_BG_DEFAULT = TFT_BLACK;
static constexpr uint16_t COL_LABEL_DEFAULT = RGB565(0x88, 0x88, 0x88);
static constexpr uint16_t COL_MUTED = RGB565(0x88, 0x88, 0x88);
static constexpr uint16_t COL_WHITE = TFT_WHITE;
static constexpr uint16_t COL_RX = RGB565(0x33, 0xAA, 0x33);
static constexpr uint16_t COL_TX = RGB565(0xCC, 0xAA, 0x33);

static uint16_t g_bg = COL_BG_DEFAULT;
static uint16_t g_label = COL_LABEL_DEFAULT;

static StatusSnap g_prev{};
static StatusLayout g_prev_layout{};
static bool g_have_prev = false;
static bool g_chrome = false;

static bool g_hole_on = false;
static int g_hole_x = 0, g_hole_y = 0, g_hole_w = 0, g_hole_h = 0;

static bool g_restore_on = false;
static int g_restore_x = 0, g_restore_y = 0, g_restore_w = 0, g_restore_h = 0;

static char g_s_host[25]{};
static char g_s_date[21]{};
static char g_s_time[13]{};
static char g_s_cpu[8]{};
static char g_s_temp[16]{};
static char g_s_mem[8]{};
static char g_s_mem_sub[24]{};
static char g_s_disk[8]{};
static char g_s_disk_sub[24]{};
static char g_s_sec_l[40]{};
static char g_s_sec_r[40]{};
static uint16_t g_c_cpu = 0, g_c_mem = 0, g_c_disk = 0;

static char g_s_if_name[STATUS_IFACE_COUNT][IFACE_NAME_SHOW + 1]{};
static char g_s_if_ip[STATUS_IFACE_COUNT][IFACE_IP_SHOW + 1]{};
static char g_s_if_rx[STATUS_IFACE_COUNT][12]{};
static char g_s_if_tx[STATUS_IFACE_COUNT][12]{};
static char g_alert_shown[STATUS_ALERT_CHARS + 1]{};
static char g_alert_latch[STATUS_ALERT_CHARS + 1]{};
static char g_alert_ack[STATUS_ALERT_CHARS + 1]{};
static uint32_t g_alert_until_ms = 0;
static bool g_alert_on = false;
static int g_drawn_iface_n = 0;

// Shared name/IP marquee offset (split phases stranded long IPs).
static constexpr uint32_t ROLL_STEP_MS = 500;
static constexpr uint32_t ROLL_PAUSE_MS = 2500;
static uint16_t g_roll_off = 0;
static int8_t g_roll_dir = 1;
static uint32_t g_roll_step_ms = 0;
static uint32_t g_roll_pause_until = 0;
static uint16_t g_roll_painted_off = 0xFFFF;

static constexpr int ALERT_PAD_X = 6;
static constexpr int ALERT_SHOW_CHARS = (320 - ALERT_PAD_X * 2) / 6;
static constexpr uint32_t ALERT_ROLL_PAUSE_MS = 1200;
static uint16_t g_alert_roll_off = 0;
static int8_t g_alert_roll_dir = 1;
static uint32_t g_alert_roll_step_ms = 0;
static uint32_t g_alert_roll_pause_until = 0;
static uint16_t g_alert_roll_painted = 0xFFFF;
static char g_alert_win_shown[ALERT_SHOW_CHARS + 1]{};

static constexpr int FONT1_H = 8;
static constexpr int FONT1_W = 6;
static constexpr int FONT4_H = 28;
static constexpr int HERO_COL_W = 100;

static void resetRoll() {
  g_roll_off = 0;
  g_roll_dir = 1;
  g_roll_step_ms = 0;
  g_roll_pause_until = 0;
  g_roll_painted_off = 0xFFFF;
}

static void resetAlertRoll() {
  g_alert_roll_off = 0;
  g_alert_roll_dir = 1;
  g_alert_roll_step_ms = 0;
  g_alert_roll_pause_until = 0;
  g_alert_roll_painted = 0xFFFF;
  g_alert_win_shown[0] = 0;
}

void statusGuiReset() {
  g_have_prev = false;
  g_chrome = false;
  g_drawn_iface_n = 0;
  g_bg = COL_BG_DEFAULT;
  g_label = COL_LABEL_DEFAULT;
  g_hole_on = false;
  resetRoll();
  memset(&g_prev, 0, sizeof(g_prev));
  memset(&g_prev_layout, 0, sizeof(g_prev_layout));
  memset(g_s_host, 0, sizeof(g_s_host));
  memset(g_s_date, 0, sizeof(g_s_date));
  memset(g_s_time, 0, sizeof(g_s_time));
  memset(g_s_cpu, 0, sizeof(g_s_cpu));
  memset(g_s_temp, 0, sizeof(g_s_temp));
  memset(g_s_mem, 0, sizeof(g_s_mem));
  memset(g_s_mem_sub, 0, sizeof(g_s_mem_sub));
  memset(g_s_disk, 0, sizeof(g_s_disk));
  memset(g_s_disk_sub, 0, sizeof(g_s_disk_sub));
  memset(g_s_sec_l, 0, sizeof(g_s_sec_l));
  memset(g_s_sec_r, 0, sizeof(g_s_sec_r));
  memset(g_s_if_name, 0, sizeof(g_s_if_name));
  memset(g_s_if_ip, 0, sizeof(g_s_if_ip));
  memset(g_s_if_rx, 0, sizeof(g_s_if_rx));
  memset(g_s_if_tx, 0, sizeof(g_s_if_tx));
  g_c_cpu = g_c_mem = g_c_disk = 0;
  g_alert_shown[0] = 0;
  g_alert_latch[0] = 0;
  g_alert_ack[0] = 0;
  g_alert_until_ms = 0;
  g_alert_on = false;
  resetAlertRoll();
}

bool statusGuiAlertActive() { return g_alert_on; }

void statusGuiDismissAlert() {
  if (!g_alert_on)
    return;
  strncpy(g_alert_ack, g_alert_shown, STATUS_ALERT_CHARS);
  g_alert_ack[STATUS_ALERT_CHARS] = 0;
}

void statusGuiSetOverlay(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) {
    g_hole_on = false;
    return;
  }
  g_hole_x = x;
  g_hole_y = y;
  g_hole_w = w;
  g_hole_h = h;
  g_hole_on = true;
}

static bool rectsOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw,
                         int bh) {
  if (aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0)
    return false;
  return !(ax + aw <= bx || ax >= bx + bw || ay + ah <= by || ay >= by + bh);
}

static bool hitsRestore(int x, int y, int w, int h) {
  return g_restore_on &&
         rectsOverlap(x, y, w, h, g_restore_x, g_restore_y, g_restore_w,
                      g_restore_h);
}

static bool hitsHole(int x, int y, int w, int h) {
  if (!g_hole_on || w <= 0 || h <= 0)
    return false;
  return rectsOverlap(x, y, w, h, g_hole_x, g_hole_y, g_hole_w, g_hole_h);
}

static void applyTheme(const StatusStyle &st) {
  g_bg = st.bg_c;
  g_label = st.label_c ? st.label_c : COL_LABEL_DEFAULT;
}

static void clearRect(TFT_eSPI *tft, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0 || hitsHole(x, y, w, h))
    return;
  tft->fillRect(x, y, w, h, g_bg);
}

/** Full wipe that leaves the OSD overlay hole intact. */
static void clearScreenExceptHole(TFT_eSPI *tft) {
  if (!g_hole_on) {
    tft->fillScreen(g_bg);
    return;
  }
  const int hx = g_hole_x, hy = g_hole_y, hw = g_hole_w, hh = g_hole_h;
  if (hy > 0)
    tft->fillRect(0, 0, 320, hy, g_bg);
  if (hy + hh < 240)
    tft->fillRect(0, hy + hh, 320, 240 - (hy + hh), g_bg);
  if (hx > 0)
    tft->fillRect(0, hy, hx, hh, g_bg);
  if (hx + hw < 320)
    tft->fillRect(hx + hw, hy, 320 - (hx + hw), hh, g_bg);
}

static void drawText1(TFT_eSPI *tft, int x, int y, const char *t, uint16_t col) {
  if (!t || hitsHole(x, y, (int)strlen(t) * FONT1_W, FONT1_H))
    return;
  tft->setTextDatum(TL_DATUM);
  tft->setTextFont(1);
  tft->setTextColor(col, g_bg);
  tft->drawString(t, x, y, 1);
}

static void drawBig(TFT_eSPI *tft, int x, int y, const char *t, uint16_t col) {
  if (!t)
    return;
  tft->setTextFont(4);
  const int tw = tft->textWidth(t, 4);
  if (hitsHole(x, y, tw, FONT4_H))
    return;
  tft->setTextDatum(TL_DATUM);
  // Transparent: Font4's opaque pad would punch the label row above.
  tft->setTextColor(col);
  tft->drawString(t, x, y, 4);
}

static size_t cstrLen(const char *s, size_t max_n) {
  size_t i = 0;
  while (i < max_n && s[i])
    i++;
  return i;
}

static void copyCapped(char *dst, size_t dst_n, const char *src, size_t src_n) {
  size_t i = 0;
  for (; i + 1 < dst_n && i < src_n && src[i]; i++)
    dst[i] = src[i];
  dst[i] = 0;
}

/** Marquee window; `off` ignored when `src` fits in `show`. */
static void rollWindow(char *dst, size_t show, const char *src, size_t src_n,
                       uint16_t off) {
  size_t len = cstrLen(src, src_n);
  if (len <= show) {
    size_t i = 0;
    for (; i < len; i++)
      dst[i] = src[i];
    dst[i] = 0;
    return;
  }
  size_t max_off = len - show;
  if (off > max_off)
    off = (uint16_t)max_off;
  for (size_t i = 0; i < show; i++)
    dst[i] = src[off + i];
  dst[show] = 0;
}

static size_t rollExtraName(const StatusSnap &s) {
  size_t extra = 0;
  const int n = statusIfaceCount(s);
  for (int i = 0; i < n; i++) {
    size_t nl = cstrLen(s.ifaces[i].name, IFACE_NAME_WIRE);
    if (nl > IFACE_NAME_SHOW && nl - IFACE_NAME_SHOW > extra)
      extra = nl - IFACE_NAME_SHOW;
  }
  return extra;
}

static size_t rollExtraIp(const StatusSnap &s) {
  size_t extra = 0;
  const int n = statusIfaceCount(s);
  for (int i = 0; i < n; i++) {
    size_t il = cstrLen(s.ifaces[i].ip, IFACE_IP_WIRE);
    if (il > IFACE_IP_SHOW && il - IFACE_IP_SHOW > extra)
      extra = il - IFACE_IP_SHOW;
  }
  return extra;
}

static size_t rollExtraMax(const StatusSnap &s) {
  const size_t a = rollExtraName(s);
  const size_t b = rollExtraIp(s);
  return a > b ? a : b;
}

static bool advanceRoll(uint32_t now, const StatusSnap &s) {
  const size_t extra = rollExtraMax(s);
  if (extra == 0) {
    if (g_roll_off != 0) {
      g_roll_off = 0;
      g_roll_dir = 1;
      return true;
    }
    return false;
  }
  if ((int32_t)(now - g_roll_pause_until) < 0)
    return false;
  if (g_roll_step_ms != 0 && (now - g_roll_step_ms) < ROLL_STEP_MS)
    return false;
  g_roll_step_ms = now;

  int next = (int)g_roll_off + g_roll_dir;
  if (next <= 0) {
    g_roll_off = 0;
    g_roll_dir = 1;
    g_roll_pause_until = now + ROLL_PAUSE_MS;
    return true;
  }
  if ((size_t)next >= extra) {
    g_roll_off = (uint16_t)extra;
    g_roll_dir = -1;
    g_roll_pause_until = now + ROLL_PAUSE_MS;
    return true;
  }
  g_roll_off = (uint16_t)next;
  return true;
}

static void fmtRate(char *buf, size_t n, uint32_t bps) {
  if (bps >= 1000000)
    snprintf(buf, n, "%.1fM", bps / 1000000.0);
  else if (bps >= 1000)
    snprintf(buf, n, "%.0fK", bps / 1000.0);
  else
    snprintf(buf, n, "%luB", (unsigned long)bps);
}

static void fmtUptime(char *buf, size_t n, uint32_t sec) {
  uint32_t w = sec / 604800;
  uint32_t d = (sec / 86400) % 7;
  uint32_t h = (sec / 3600) % 24;
  uint32_t m = (sec / 60) % 60;
  if (w)
    snprintf(buf, n, "%luW %luD %luH %luM", (unsigned long)w, (unsigned long)d,
             (unsigned long)h, (unsigned long)m);
  else if (d)
    snprintf(buf, n, "%luD %luH %luM", (unsigned long)d, (unsigned long)h,
             (unsigned long)m);
  else if (h)
    snprintf(buf, n, "%luH %luM", (unsigned long)h, (unsigned long)m);
  else
    snprintf(buf, n, "%luM", (unsigned long)m);
}

static void fmtGb(char *buf, size_t n, uint32_t mb) {
  double gb = mb / 1024.0;
  if (gb >= 100.0)
    snprintf(buf, n, "%.0f", gb);
  else
    snprintf(buf, n, "%.1f", gb);
}

static uint16_t levelColor(const StatusStyle &st, uint8_t pct) {
  if (pct >= st.crit_at)
    return st.level_crit ? st.level_crit : RGB565(0xCC, 0x33, 0x33);
  if (pct >= st.warn_at)
    return st.level_warn ? st.level_warn : RGB565(0xCC, 0xCC, 0x33);
  return st.level_ok ? st.level_ok : RGB565(0x33, 0xAA, 0x33);
}

static uint16_t heroMeterColor(const StatusSnap &s, uint8_t which, uint8_t pct) {
  const StatusStyle &st = s.style;
  if (!st.meter_mode) {
    if (which == 0)
      return st.hero_cpu_c ? st.hero_cpu_c : COL_WHITE;
    if (which == 1)
      return st.hero_mem_c ? st.hero_mem_c : COL_WHITE;
    return st.hero_disk_c ? st.hero_disk_c : COL_WHITE;
  }
  return levelColor(st, pct);
}

static uint16_t svcColor(const StatusStyle &st, uint8_t status) {
  switch (status) {
  case ST_SVC_ACTIVE:
    return st.svc_active ? st.svc_active : RGB565(0x33, 0xAA, 0x33);
  case ST_SVC_FAILED:
    return st.svc_failed ? st.svc_failed : RGB565(0xCC, 0x33, 0x33);
  case ST_SVC_DEACTIVATING:
    return st.svc_deactivating ? st.svc_deactivating : RGB565(0xCC, 0xAA, 0x33);
  case ST_SVC_ACTIVATING:
    return st.svc_activating ? st.svc_activating : RGB565(0x33, 0x99, 0xCC);
  case ST_SVC_RELOADING:
    return st.svc_reloading ? st.svc_reloading : RGB565(0x33, 0x99, 0xCC);
  case ST_SVC_INACTIVE:
    return st.svc_inactive ? st.svc_inactive : COL_MUTED;
  default:
    return st.svc_maintenance ? st.svc_maintenance : g_label;
  }
}

static const char *secLabel(uint8_t id) {
  switch (id) {
  case SEC_UPTIME:
    return "UPTIME:";
  case SEC_SWAP:
    return "SWAP:";
  case SEC_LOAD:
    return "LOAD:";
  default:
    return "";
  }
}

static void fmtSecValue(char *buf, size_t n, const StatusSnap &s, uint8_t id) {
  char a[12], b[12];
  switch (id) {
  case SEC_UPTIME:
    fmtUptime(buf, n, s.uptime_sec);
    break;
  case SEC_SWAP:
    if (s.swap_pct == 255 || s.swap_total_mb == 0) {
      snprintf(buf, n, "--");
    } else {
      fmtGb(a, sizeof(a), s.swap_used_mb);
      fmtGb(b, sizeof(b), s.swap_total_mb);
      snprintf(buf, n, "%u%% %s/%sGB", (unsigned)s.swap_pct, a, b);
    }
    break;
  case SEC_LOAD:
    snprintf(buf, n, "%.2f %.2f %.2f", s.load_x100[0] / 100.0f,
             s.load_x100[1] / 100.0f, s.load_x100[2] / 100.0f);
    break;
  default:
    buf[0] = 0;
    break;
  }
}

bool statusGuiNeedsRoll(const StatusSnap &s) {
  if (rollExtraMax(s) == 0)
    return false;
  return advanceRoll(millis(), s);
}

static size_t alertLineExtra(const char *text) {
  char line[STATUS_ALERT_CHARS + 4];
  snprintf(line, sizeof(line), "! %s", text);
  const size_t n = strlen(line);
  if (n <= (size_t)ALERT_SHOW_CHARS)
    return 0;
  return n - (size_t)ALERT_SHOW_CHARS;
}

static uint32_t alertRollStepMs(size_t extra) {
  if (extra <= 4)
    return 320;
  if (extra <= 12)
    return 180;
  if (extra <= 28)
    return 100;
  return 60;
}

static bool advanceAlertRoll(uint32_t now, size_t extra) {
  if (extra == 0) {
    if (g_alert_roll_off != 0) {
      g_alert_roll_off = 0;
      g_alert_roll_dir = 1;
      return true;
    }
    return false;
  }
  if ((int32_t)(now - g_alert_roll_pause_until) < 0)
    return false;
  const uint32_t step = alertRollStepMs(extra);
  if (g_alert_roll_step_ms != 0 && (now - g_alert_roll_step_ms) < step)
    return false;
  g_alert_roll_step_ms = now;

  int next = (int)g_alert_roll_off + g_alert_roll_dir;
  if (next <= 0) {
    g_alert_roll_off = 0;
    g_alert_roll_dir = 1;
    g_alert_roll_pause_until = now + ALERT_ROLL_PAUSE_MS;
    return true;
  }
  if ((size_t)next >= extra) {
    g_alert_roll_off = (uint16_t)extra;
    g_alert_roll_dir = -1;
    g_alert_roll_pause_until = now + ALERT_ROLL_PAUSE_MS;
    return true;
  }
  g_alert_roll_off = (uint16_t)next;
  return true;
}

bool statusGuiNeedsAlertTick() {
  const uint32_t now = millis();
  const size_t extra =
      (g_alert_on && g_alert_shown[0]) ? alertLineExtra(g_alert_shown) : 0;
  const bool rolled = advanceAlertRoll(now, extra);
  if (g_alert_until_ms != 0 && g_alert_latch[0] != 0 &&
      (int32_t)(now - g_alert_until_ms) >= 0)
    return true;
  return rolled;
}

static bool styleChanged(const StatusSnap &a, const StatusSnap &b) {
  return memcmp(&a.style, &b.style, sizeof(StatusStyle)) != 0;
}

static bool layoutChanged(const StatusLayout &a, const StatusLayout &b) {
  return a.y_hero_label != b.y_hero_label || a.y_secondary != b.y_secondary ||
         a.y_net0 != b.y_net0 || a.y_services != b.y_services ||
         a.secondary != b.secondary || a.iface_n != b.iface_n;
}

static bool servicesEqual(const StatusSnap &a, const StatusSnap &b) {
  const int n = statusSvcCount(a);
  if (n != statusSvcCount(b))
    return false;
  for (int i = 0; i < n; i++) {
    if (a.services[i].status != b.services[i].status ||
        memcmp(a.services[i].name, b.services[i].name, STATUS_SVC_NAME) != 0)
      return false;
  }
  return true;
}

static bool paintSlot1(TFT_eSPI *tft, int x, int y, int slot_w, char *cache,
                       size_t cache_n, const char *text, uint16_t col,
                       bool force) {
  const bool restore = hitsRestore(x, y, slot_w, FONT1_H);
  if (!force && !restore && strncmp(cache, text, cache_n) == 0)
    return false;
  if (restore)
    clearRect(tft, x, y, slot_w, FONT1_H);
  const int old_w = cache[0] ? (int)strlen(cache) * FONT1_W : 0;
  drawText1(tft, x, y, text, col);
  const int new_w = (int)strlen(text) * FONT1_W;
  if (old_w > new_w)
    clearRect(tft, x + new_w, y, old_w - new_w, FONT1_H);
  else if (new_w < slot_w && (force || restore))
    clearRect(tft, x + new_w, y, slot_w - new_w, FONT1_H);
  strncpy(cache, text, cache_n - 1);
  cache[cache_n - 1] = 0;
  return true;
}

static bool paintSlotBig(TFT_eSPI *tft, int x, int y, int slot_w, char *cache,
                         size_t cache_n, const char *text, uint16_t col,
                         uint16_t *col_cache, bool force) {
  const bool restore = hitsRestore(x, y, slot_w, FONT4_H);
  if (!force && !restore && strncmp(cache, text, cache_n) == 0 &&
      *col_cache == col)
    return false;
  tft->setTextFont(4);
  const int old_w = cache[0] ? tft->textWidth(cache, 4) : slot_w;
  const int new_w = tft->textWidth(text, 4);
  int cw = old_w > new_w ? old_w : new_w;
  if (cw > slot_w)
    cw = slot_w;
  if (restore)
    clearRect(tft, x, y, slot_w, FONT4_H);
  else
    clearRect(tft, x, y, cw, FONT4_H);
  drawBig(tft, x, y, text, col);
  strncpy(cache, text, cache_n - 1);
  cache[cache_n - 1] = 0;
  *col_cache = col;
  return true;
}

static void paintHeroLabels(TFT_eSPI *tft, const StatusLayout &L) {
  drawText1(tft, 6, L.y_hero_label, "CPU", g_label);
  drawText1(tft, 112, L.y_hero_label, "MEM", g_label);
  drawText1(tft, 218, L.y_hero_label, "DISK", g_label);
  drawText1(tft, 6, L.y_hero_sub, "TEMP:", g_label);
}

static void paintChrome(TFT_eSPI *tft, const StatusSnap &s,
                        const StatusLayout &L) {
  paintHeroLabels(tft, L);

  if (L.secondary) {
    if (s.style.sec_left != SEC_NONE)
      drawText1(tft, 6, L.y_secondary, secLabel(s.style.sec_left), g_label);
    if (s.style.sec_right != SEC_NONE)
      drawText1(tft, 168, L.y_secondary, secLabel(s.style.sec_right), g_label);
  }

  for (int i = 0; i < L.iface_n; i++) {
    const int y = L.y_net0 + i * STATUS_IFACE_STEP;
    drawText1(tft, 200, y, "RX:", COL_RX);
    drawText1(tft, 258, y, "TX:", COL_TX);
  }

  drawText1(tft, 6, L.y_services, "SERVICES", g_label);
}

static void paintHeader(TFT_eSPI *tft, const StatusSnap &s, bool force) {
  char host[25], date[21], tim[13];
  copyCapped(host, sizeof(host), s.hostname, sizeof(s.hostname));
  if (!host[0])
    snprintf(host, sizeof(host), "host");
  copyCapped(date, sizeof(date), s.date, sizeof(s.date));
  if (!date[0])
    snprintf(date, sizeof(date), "--/--/----");
  copyCapped(tim, sizeof(tim), s.time, sizeof(s.time));
  if (!tim[0])
    snprintf(tim, sizeof(tim), "--:--:--");

  // Per-field slots — avoid wiping the full 320×16 header on clock ticks.
  paintSlot1(tft, 4, 4, 120, g_s_host, sizeof(g_s_host), host,
             s.style.host_c ? s.style.host_c : COL_WHITE, force);

  constexpr int time_w = 8 * FONT1_W;
  constexpr int time_x = 316 - time_w;
  const bool time_restore = hitsRestore(time_x, 4, time_w, FONT1_H);
  if (force || time_restore || strncmp(g_s_time, tim, sizeof(g_s_time)) != 0) {
    if (!hitsHole(time_x, 4, time_w, FONT1_H)) {
      if (time_restore)
        clearRect(tft, time_x, 4, time_w, FONT1_H);
      const int old_w = g_s_time[0] ? (int)strlen(g_s_time) * FONT1_W : time_w;
      tft->setTextDatum(TR_DATUM);
      tft->setTextFont(1);
      tft->setTextColor(s.style.time_c ? s.style.time_c : COL_WHITE, g_bg);
      tft->drawString(tim, 316, 4, 1);
      tft->setTextDatum(TL_DATUM);
      const int new_w = (int)strlen(tim) * FONT1_W;
      if (old_w > new_w)
        clearRect(tft, 316 - old_w, 4, old_w - new_w, FONT1_H);
    }
    strncpy(g_s_time, tim, sizeof(g_s_time) - 1);
  }

  constexpr int date_w = 10 * FONT1_W;
  constexpr int date_x = time_x - 6 - date_w;
  paintSlot1(tft, date_x, 4, date_w, g_s_date, sizeof(g_s_date), date,
             s.style.date_c ? s.style.date_c : g_label, force);
}

static void paintHero(TFT_eSPI *tft, const StatusSnap &s, const StatusLayout &L,
                      bool force) {
  char line[24], a[12], b[12];
  bool touched = false;

  if (s.flags & ST_F_HAS_CPU)
    snprintf(line, sizeof(line), "%u%%", (unsigned)s.cpu_pct);
  else
    snprintf(line, sizeof(line), "--");
  touched |= paintSlotBig(tft, 6, L.y_hero_pct, HERO_COL_W - 8, g_s_cpu,
                          sizeof(g_s_cpu), line,
                          (s.flags & ST_F_HAS_CPU)
                              ? heroMeterColor(s, 0, s.cpu_pct)
                              : COL_MUTED,
                          &g_c_cpu, force);

  if (s.flags & ST_F_HAS_TEMP)
    snprintf(line, sizeof(line), "%.0fC", s.cpu_temp_c10 / 10.0f);
  else
    snprintf(line, sizeof(line), "--");
  touched |= paintSlot1(tft, 6 + 5 * FONT1_W, L.y_hero_sub, 48, g_s_temp,
                        sizeof(g_s_temp), line, COL_WHITE, force);

  if (s.flags & ST_F_HAS_MEM)
    snprintf(line, sizeof(line), "%u%%", (unsigned)s.mem_pct);
  else
    snprintf(line, sizeof(line), "--");
  touched |= paintSlotBig(tft, 112, L.y_hero_pct, HERO_COL_W - 8, g_s_mem,
                          sizeof(g_s_mem), line,
                          (s.flags & ST_F_HAS_MEM)
                              ? heroMeterColor(s, 1, s.mem_pct)
                              : COL_MUTED,
                          &g_c_mem, force);

  if (s.flags & ST_F_HAS_MEM) {
    fmtGb(a, sizeof(a), s.mem_used_mb);
    fmtGb(b, sizeof(b), s.mem_total_mb);
    snprintf(line, sizeof(line), "%s/%s GB", a, b);
  } else {
    snprintf(line, sizeof(line), "--/-- GB");
  }
  touched |= paintSlot1(tft, 112, L.y_hero_sub, HERO_COL_W - 8, g_s_mem_sub,
                        sizeof(g_s_mem_sub), line, COL_WHITE, force);

  if (s.flags & ST_F_HAS_DISK)
    snprintf(line, sizeof(line), "%u%%", (unsigned)s.disk_pct);
  else
    snprintf(line, sizeof(line), "--");
  touched |= paintSlotBig(tft, 218, L.y_hero_pct, HERO_COL_W - 8, g_s_disk,
                          sizeof(g_s_disk), line,
                          (s.flags & ST_F_HAS_DISK)
                              ? heroMeterColor(s, 2, s.disk_pct)
                              : COL_MUTED,
                          &g_c_disk, force);

  if (s.flags & ST_F_HAS_DISK) {
    fmtGb(a, sizeof(a), s.disk_used_mb);
    fmtGb(b, sizeof(b), s.disk_total_mb);
    snprintf(line, sizeof(line), "%s/%s GB", a, b);
  } else {
    snprintf(line, sizeof(line), "--/-- GB");
  }
  touched |= paintSlot1(tft, 218, L.y_hero_sub, HERO_COL_W - 8, g_s_disk_sub,
                        sizeof(g_s_disk_sub), line, COL_WHITE, force);

  // Font4 clears can clip the CPU/MEM/DISK labels above.
  if (touched || force)
    paintHeroLabels(tft, L);
}

static void paintSecondary(TFT_eSPI *tft, const StatusSnap &s,
                           const StatusLayout &L, bool force) {
  if (!L.secondary)
    return;
  char val[40];
  if (s.style.sec_left != SEC_NONE) {
    fmtSecValue(val, sizeof(val), s, s.style.sec_left);
    paintSlot1(tft, 54, L.y_secondary, 110, g_s_sec_l, sizeof(g_s_sec_l), val,
               s.style.sec_left_c ? s.style.sec_left_c : COL_WHITE, force);
  }
  if (s.style.sec_right != SEC_NONE) {
    fmtSecValue(val, sizeof(val), s, s.style.sec_right);
    paintSlot1(tft, 210, L.y_secondary, 110, g_s_sec_r, sizeof(g_s_sec_r), val,
               s.style.sec_right_c ? s.style.sec_right_c : COL_WHITE, force);
  }
}

static void paintNet(TFT_eSPI *tft, const StatusSnap &s, const StatusLayout &L,
                     bool force) {
  const int n = L.iface_n;
  const int x_name = 6;
  const int x_ip = 6 + (int)IFACE_NAME_SHOW * FONT1_W + 8;
  const int name_w = (int)IFACE_NAME_SHOW * FONT1_W;
  const int ip_w = (int)IFACE_IP_SHOW * FONT1_W;
  const int rate_w = 40;
  const uint16_t name_off = g_roll_off;
  const uint16_t ip_off = g_roll_off;

  if (force || n != g_drawn_iface_n) {
    const int wipe = (n > g_drawn_iface_n ? n : g_drawn_iface_n);
    const int y0 = L.y_net0;
    if (wipe > 0)
      clearRect(tft, 0, y0 - 1, 320, wipe * STATUS_IFACE_STEP + 2);
    for (int i = 0; i < n; i++) {
      const int y = L.y_net0 + i * STATUS_IFACE_STEP;
      drawText1(tft, 200, y, "RX:", COL_RX);
      drawText1(tft, 258, y, "TX:", COL_TX);
    }
    for (int i = 0; i < STATUS_IFACE_COUNT; i++) {
      g_s_if_name[i][0] = 0;
      g_s_if_ip[i][0] = 0;
      g_s_if_rx[i][0] = 0;
      g_s_if_tx[i][0] = 0;
    }
    g_drawn_iface_n = n;
  }

  for (int i = 0; i < n; i++) {
    const StatusIface &cur = s.ifaces[i];
    const int y = L.y_net0 + i * STATUS_IFACE_STEP;
    if (!cur.name[0])
      continue;

    char name[IFACE_NAME_SHOW + 1];
    char ip[IFACE_IP_SHOW + 1];
    char rx[12], tx[12];

    rollWindow(name, IFACE_NAME_SHOW, cur.name, IFACE_NAME_WIRE, name_off);
    if (cur.ip[0])
      rollWindow(ip, IFACE_IP_SHOW, cur.ip, IFACE_IP_WIRE, ip_off);
    else {
      ip[0] = '-';
      ip[1] = 0;
    }
    fmtRate(rx, sizeof(rx), cur.rx_bps);
    fmtRate(tx, sizeof(tx), cur.tx_bps);

    paintSlot1(tft, x_name, y, name_w, g_s_if_name[i], sizeof(g_s_if_name[i]),
               name, g_label, force);
    paintSlot1(tft, x_ip, y, ip_w, g_s_if_ip[i], sizeof(g_s_if_ip[i]), ip,
               COL_WHITE, force);
    paintSlot1(tft, 218, y, rate_w, g_s_if_rx[i], sizeof(g_s_if_rx[i]), rx,
               COL_WHITE, force);
    paintSlot1(tft, 276, y, rate_w, g_s_if_tx[i], sizeof(g_s_if_tx[i]), tx,
               COL_WHITE, force);
  }
}

static bool servicesNamesEqual(const StatusSnap &a, const StatusSnap &b) {
  const int n = statusSvcCount(a);
  if (n != statusSvcCount(b))
    return false;
  for (int i = 0; i < n; i++) {
    if (memcmp(a.services[i].name, b.services[i].name, STATUS_SVC_NAME) != 0)
      return false;
  }
  return true;
}

static void paintServices(TFT_eSPI *tft, const StatusSnap &s,
                          const StatusLayout &L, bool force, int max_y) {
  if (!force && g_have_prev && servicesEqual(s, g_prev))
    return;

  const int y0 = L.y_services;
  const int n = statusSvcCount(s);
  const int body_y = y0 + 12;
  const bool wipe =
      force || !g_have_prev || !servicesNamesEqual(s, g_prev) ||
      (g_alert_on ? (240 - STATUS_ALERT_H) : 240) != max_y;

  if (wipe) {
    clearRect(tft, 0, body_y - 1, 320, max_y - (body_y - 1));
    drawText1(tft, 6, y0, "SERVICES", g_label);
  }

  int x = 6;
  int y = body_y;
  const int max_x = 314;
  const int row_h = 11;
  for (int i = 0; i < n; i++) {
    char name[STATUS_SVC_NAME + 1];
    memcpy(name, s.services[i].name, STATUS_SVC_NAME);
    name[STATUS_SVC_NAME] = 0;
    if (!name[0])
      continue;
    const int w = (int)strlen(name) * FONT1_W;
    if (x > 6 && x + w > max_x) {
      x = 6;
      y += row_h;
    }
    if (y + 8 > max_y)
      break;
    drawText1(tft, x, y, name, svcColor(s.style, s.services[i].status));
    x += w + 8;
  }
}

static void resolveAlertText(const StatusSnap &s, uint32_t now_ms, char *out,
                             size_t out_n, bool *active) {
  char live[STATUS_ALERT_CHARS + 1];
  statusBuildAlert(s, live, sizeof(live));
  if (!live[0]) {
    g_alert_latch[0] = 0;
    g_alert_ack[0] = 0;
    g_alert_until_ms = 0;
    out[0] = 0;
    *active = false;
    return;
  }

  if (strncmp(g_alert_latch, live, STATUS_ALERT_CHARS) != 0) {
    strncpy(g_alert_latch, live, STATUS_ALERT_CHARS);
    g_alert_latch[STATUS_ALERT_CHARS] = 0;
    g_alert_until_ms = s.style.alert_hold_sec > 0
                           ? now_ms + (uint32_t)s.style.alert_hold_sec * 1000u
                           : 0;
    resetAlertRoll();
  }

  // Tap-dismissed; stays hidden until alert text changes.
  if (g_alert_ack[0] && strncmp(g_alert_ack, live, STATUS_ALERT_CHARS) == 0) {
    out[0] = 0;
    *active = false;
    return;
  }

  if (s.style.alert_hold_sec > 0 && g_alert_until_ms != 0 &&
      (int32_t)(now_ms - g_alert_until_ms) >= 0) {
    out[0] = 0;
    *active = false;
    return;
  }

  strncpy(out, live, out_n - 1);
  out[out_n - 1] = 0;
  *active = true;
}

static void paintAlertStrip(TFT_eSPI *tft, const StatusSnap &s, const char *text,
                            bool full) {
  const int y = 240 - STATUS_ALERT_H;
  const int text_y = y + (STATUS_ALERT_H - FONT1_H) / 2;
  const uint16_t bg = s.style.alert_bg_c ? s.style.alert_bg_c : 0x9800;
  const uint16_t fg = s.style.alert_fg_c ? s.style.alert_fg_c : COL_WHITE;
  char line[STATUS_ALERT_CHARS + 4];
  snprintf(line, sizeof(line), "! %s", text);
  char win[ALERT_SHOW_CHARS + 1];
  rollWindow(win, ALERT_SHOW_CHARS, line, sizeof(line) - 1, g_alert_roll_off);
  // Pad with spaces so opaque cells clear the previous tail.
  const size_t wlen = strlen(win);
  for (size_t i = wlen; i < (size_t)ALERT_SHOW_CHARS; i++)
    win[i] = ' ';
  win[ALERT_SHOW_CHARS] = 0;

  if (!full && memcmp(win, g_alert_win_shown, ALERT_SHOW_CHARS) == 0)
    return;

  if (full) {
    tft->fillRect(0, y, 320, STATUS_ALERT_H, bg);
    memset(g_alert_win_shown, 0, sizeof(g_alert_win_shown));
  }

  tft->setTextFont(1);
  for (int i = 0; i < ALERT_SHOW_CHARS; i++) {
    if (!full && win[i] == g_alert_win_shown[i])
      continue;
    tft->drawChar(ALERT_PAD_X + i * FONT1_W, text_y, win[i], fg, bg, 1);
    g_alert_win_shown[i] = win[i];
  }
  g_alert_win_shown[ALERT_SHOW_CHARS] = 0;
}

void paintStatusGui(TFT_eSPI *tft, const StatusSnap &s, bool force_full) {
  if (!tft)
    return;

  applyTheme(s.style);
  const StatusLayout L = statusLayoutOf(s);
  const bool force =
      force_full || !g_chrome || !g_have_prev || styleChanged(s, g_prev) ||
      layoutChanged(L, g_prev_layout);

  char alert[STATUS_ALERT_CHARS + 1];
  bool alert_active = false;
  resolveAlertText(s, millis(), alert, sizeof(alert), &alert_active);
  const bool alert_changed =
      alert_active != g_alert_on || strcmp(alert, g_alert_shown) != 0;
  const bool alert_band_changed = alert_active != g_alert_on;
  const int svc_max_y = alert_active ? (240 - STATUS_ALERT_H) : 240;
  const bool force_services = force || alert_band_changed;

  if (force) {
    clearScreenExceptHole(tft);
    g_have_prev = false;
    g_drawn_iface_n = 0;
    resetRoll();
    memset(g_s_host, 0, sizeof(g_s_host));
    memset(g_s_date, 0, sizeof(g_s_date));
    memset(g_s_time, 0, sizeof(g_s_time));
    memset(g_s_cpu, 0, sizeof(g_s_cpu));
    memset(g_s_temp, 0, sizeof(g_s_temp));
    memset(g_s_mem, 0, sizeof(g_s_mem));
    memset(g_s_mem_sub, 0, sizeof(g_s_mem_sub));
    memset(g_s_disk, 0, sizeof(g_s_disk));
    memset(g_s_disk_sub, 0, sizeof(g_s_disk_sub));
    memset(g_s_sec_l, 0, sizeof(g_s_sec_l));
    memset(g_s_sec_r, 0, sizeof(g_s_sec_r));
    memset(g_s_if_name, 0, sizeof(g_s_if_name));
    memset(g_s_if_ip, 0, sizeof(g_s_if_ip));
    memset(g_s_if_rx, 0, sizeof(g_s_if_rx));
    memset(g_s_if_tx, 0, sizeof(g_s_if_tx));
    g_c_cpu = g_c_mem = g_c_disk = 0;
    g_chrome = true;
    paintChrome(tft, s, L);
  } else if (g_have_prev) {
    for (int i = 0; i < STATUS_IFACE_COUNT; i++) {
      if (memcmp(s.ifaces[i].name, g_prev.ifaces[i].name, IFACE_NAME_WIRE) !=
              0 ||
          memcmp(s.ifaces[i].ip, g_prev.ifaces[i].ip, IFACE_IP_WIRE) != 0) {
        g_roll_off = 0;
        g_roll_dir = 1;
        g_roll_painted_off = 0xFFFF;
        break;
      }
    }
  }

  paintHeader(tft, s, force);
  paintHero(tft, s, L, force);
  paintSecondary(tft, s, L, force);
  paintNet(tft, s, L, force);

  // Dismiss strip before services paint into that band.
  if (!alert_active && g_alert_on) {
    clearRect(tft, 0, 240 - STATUS_ALERT_H, 320, STATUS_ALERT_H);
    resetAlertRoll();
  }

  paintServices(tft, s, L, force_services, svc_max_y);

  if (alert_active) {
    const bool roll_changed = g_alert_roll_painted != g_alert_roll_off;
    if (force || alert_changed || roll_changed)
      paintAlertStrip(tft, s, alert, force || alert_changed);
    g_alert_roll_painted = g_alert_roll_off;
  }

  strncpy(g_alert_shown, alert, STATUS_ALERT_CHARS);
  g_alert_shown[STATUS_ALERT_CHARS] = 0;
  g_alert_on = alert_active;

  g_prev = s;
  g_prev_layout = L;
  g_have_prev = true;
}

void statusGuiRestoreRegion(TFT_eSPI *tft, const StatusSnap &s, int x, int y,
                            int w, int h) {
  if (!tft || w <= 0 || h <= 0)
    return;
  applyTheme(s.style);
  tft->fillRect(x, y, w, h, g_bg);
  g_restore_on = true;
  g_restore_x = x;
  g_restore_y = y;
  g_restore_w = w;
  g_restore_h = h;

  const StatusLayout L = statusLayoutOf(s);
  if (hitsRestore(6, L.y_hero_label, 300, FONT1_H) ||
      hitsRestore(6, L.y_hero_sub, 48, FONT1_H))
    paintHeroLabels(tft, L);
  if (L.secondary && hitsRestore(6, L.y_secondary, 300, FONT1_H)) {
    if (s.style.sec_left != SEC_NONE)
      drawText1(tft, 6, L.y_secondary, secLabel(s.style.sec_left), g_label);
    if (s.style.sec_right != SEC_NONE)
      drawText1(tft, 168, L.y_secondary, secLabel(s.style.sec_right), g_label);
  }
  for (int i = 0; i < L.iface_n; i++) {
    const int iy = L.y_net0 + i * STATUS_IFACE_STEP;
    if (hitsRestore(200, iy, 100, FONT1_H)) {
      drawText1(tft, 200, iy, "RX:", COL_RX);
      drawText1(tft, 258, iy, "TX:", COL_TX);
    }
  }

  paintHeader(tft, s, false);
  paintHero(tft, s, L, false);
  paintSecondary(tft, s, L, false);
  paintNet(tft, s, L, false);

  g_restore_on = false;
}
