#include "status_ui.h"

#include <stdio.h>

#define RGB565(r, g, b)                                                        \
  (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

static constexpr uint16_t COL_BG_DEFAULT = TFT_BLACK;
static constexpr uint16_t COL_LABEL_DEFAULT = RGB565(0x6B, 0x7C, 0x8F);
static constexpr uint16_t COL_MUTED = RGB565(0xA8, 0xB4, 0xC0);
static constexpr uint16_t COL_WHITE = TFT_WHITE;
static constexpr uint16_t COL_RX = RGB565(0x81, 0xC7, 0x84);
static constexpr uint16_t COL_TX = RGB565(0xE0, 0xC0, 0x6A);

static uint16_t g_bg = COL_BG_DEFAULT;
static uint16_t g_label = COL_LABEL_DEFAULT;

static constexpr uint8_t DIRTY_HEADER = 1 << 0;
static constexpr uint8_t DIRTY_HERO = 1 << 1;
static constexpr uint8_t DIRTY_SECONDARY = 1 << 2;
static constexpr uint8_t DIRTY_NET = 1 << 3;
static constexpr uint8_t DIRTY_SERVICES = 1 << 4;
static constexpr uint8_t DIRTY_ALL = 0x1F;

static StatusSnap g_prev{};
static StatusLayout g_prev_layout{};
static bool g_have_prev = false;
static bool g_chrome = false;
static int g_prev_iface_n = 0;
static int g_prev_svc_y = -1;
static int g_prev_svc_n = 0;

static constexpr uint8_t ROLL_NAME = 0;
static constexpr uint8_t ROLL_IP = 1;
static uint16_t g_roll_off = 0;
static int8_t g_roll_dir = 1;
static uint8_t g_roll_phase = ROLL_NAME;
static uint32_t g_roll_step_ms = 0;
static uint32_t g_roll_pause_until = 0;

void statusGuiReset() {
  g_have_prev = false;
  g_chrome = false;
  g_prev_iface_n = 0;
  g_prev_svc_y = -1;
  g_prev_svc_n = 0;
  g_roll_off = 0;
  g_roll_dir = 1;
  g_roll_phase = ROLL_NAME;
  g_roll_step_ms = 0;
  g_roll_pause_until = 0;
  g_bg = COL_BG_DEFAULT;
  g_label = COL_LABEL_DEFAULT;
  memset(&g_prev, 0, sizeof(g_prev));
  memset(&g_prev_layout, 0, sizeof(g_prev_layout));
}

static void applyTheme(const StatusStyle &st) {
  g_bg = st.bg_c; // black is a valid value (0)
  g_label = st.label_c ? st.label_c : COL_LABEL_DEFAULT;
}

static void clearRect(TFT_eSPI *tft, int x, int y, int w, int h) {
  if (w <= 0 || h <= 0)
    return;
  tft->fillRect(x, y, w, h, g_bg);
}

static void drawText1(TFT_eSPI *tft, int x, int y, const char *t, uint16_t col) {
  tft->setTextDatum(TL_DATUM);
  tft->setTextFont(1);
  tft->setTextColor(col, g_bg);
  tft->drawString(t, x, y, 1);
}

static void drawBig(TFT_eSPI *tft, int x, int y, const char *t, uint16_t col) {
  tft->setTextDatum(TL_DATUM);
  tft->setTextFont(4);
  tft->setTextColor(col, g_bg);
  tft->drawString(t, x, y, 4);
}

static size_t cstrLen(const char *s, size_t max_n) {
  size_t i = 0;
  while (i < max_n && s[i])
    i++;
  return i;
}

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
    return st.level_crit ? st.level_crit : RGB565(0xE5, 0x73, 0x73);
  if (pct >= st.warn_at)
    return st.level_warn ? st.level_warn : RGB565(0xE0, 0xC0, 0x6A);
  return st.level_ok ? st.level_ok : RGB565(0x81, 0xC7, 0x84);
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
    return st.svc_active ? st.svc_active : RGB565(0x81, 0xC7, 0x84);
  case ST_SVC_FAILED:
    return st.svc_failed ? st.svc_failed : RGB565(0xE5, 0x73, 0x73);
  case ST_SVC_DEACTIVATING:
    return st.svc_deactivating ? st.svc_deactivating : RGB565(0xE0, 0xC0, 0x6A);
  case ST_SVC_ACTIVATING:
    return st.svc_activating ? st.svc_activating : RGB565(0x4D, 0xD0, 0xE1);
  case ST_SVC_RELOADING:
    return st.svc_reloading ? st.svc_reloading : RGB565(0x4D, 0xD0, 0xE1);
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

bool statusGuiNeedsRoll(const StatusSnap &s) {
  return rollExtraName(s) > 0 || rollExtraIp(s) > 0;
}

static void pickRollPhase(const StatusSnap &s) {
  const size_t n_ex = rollExtraName(s);
  const size_t i_ex = rollExtraIp(s);
  if (g_roll_phase == ROLL_NAME) {
    if (i_ex > 0)
      g_roll_phase = ROLL_IP;
  } else {
    if (n_ex > 0)
      g_roll_phase = ROLL_NAME;
  }
  g_roll_off = 0;
  g_roll_dir = 1;
}

static bool advanceRoll(uint32_t now, const StatusSnap &s) {
  size_t extra =
      (g_roll_phase == ROLL_NAME) ? rollExtraName(s) : rollExtraIp(s);
  if (extra == 0) {
    const uint8_t prev = g_roll_phase;
    pickRollPhase(s);
    extra = (g_roll_phase == ROLL_NAME) ? rollExtraName(s) : rollExtraIp(s);
    if (extra == 0) {
      if (g_roll_off != 0) {
        g_roll_off = 0;
        return true;
      }
      return g_roll_phase != prev;
    }
  }
  if ((int32_t)(now - g_roll_pause_until) < 0)
    return false;
  if (g_roll_step_ms != 0 && (now - g_roll_step_ms) < 320)
    return false;
  g_roll_step_ms = now;
  int next = (int)g_roll_off + g_roll_dir;
  if (next <= 0) {
    const bool finished = (g_roll_dir < 0);
    g_roll_off = 0;
    g_roll_dir = 1;
    g_roll_pause_until = now + 800;
    if (finished)
      pickRollPhase(s);
    return true;
  }
  if ((size_t)next >= extra) {
    g_roll_off = (uint16_t)extra;
    g_roll_dir = -1;
    g_roll_pause_until = now + 800;
    return true;
  }
  g_roll_off = (uint16_t)next;
  return true;
}

static bool styleChanged(const StatusSnap &a, const StatusSnap &b) {
  return memcmp(&a.style, &b.style, sizeof(StatusStyle)) != 0;
}

static bool layoutChanged(const StatusLayout &a, const StatusLayout &b) {
  return a.y_hero_label != b.y_hero_label || a.y_secondary != b.y_secondary ||
         a.y_net0 != b.y_net0 || a.y_services != b.y_services ||
         a.secondary != b.secondary || a.iface_n != b.iface_n;
}

static uint8_t computeDirty(const StatusSnap &s, const StatusLayout &L,
                            bool force, bool roll_tick) {
  if (force || !g_have_prev || styleChanged(s, g_prev) ||
      layoutChanged(L, g_prev_layout))
    return DIRTY_ALL;

  uint8_t d = 0;
  if (memcmp(s.hostname, g_prev.hostname, sizeof(s.hostname)) != 0 ||
      memcmp(s.date, g_prev.date, sizeof(s.date)) != 0 ||
      memcmp(s.time, g_prev.time, sizeof(s.time)) != 0)
    d |= DIRTY_HEADER;

  if (s.cpu_pct != g_prev.cpu_pct || s.mem_pct != g_prev.mem_pct ||
      s.disk_pct != g_prev.disk_pct || s.cpu_temp_c10 != g_prev.cpu_temp_c10 ||
      s.mem_used_mb != g_prev.mem_used_mb ||
      s.mem_total_mb != g_prev.mem_total_mb ||
      s.disk_used_mb != g_prev.disk_used_mb ||
      s.disk_total_mb != g_prev.disk_total_mb ||
      (s.flags & (ST_F_HAS_CPU | ST_F_HAS_MEM | ST_F_HAS_DISK | ST_F_HAS_TEMP)) !=
          (g_prev.flags &
           (ST_F_HAS_CPU | ST_F_HAS_MEM | ST_F_HAS_DISK | ST_F_HAS_TEMP)))
    d |= DIRTY_HERO;

  if (L.secondary) {
    if (s.uptime_sec != g_prev.uptime_sec || s.swap_pct != g_prev.swap_pct ||
        s.swap_used_mb != g_prev.swap_used_mb ||
        s.swap_total_mb != g_prev.swap_total_mb ||
        s.load_x100[0] != g_prev.load_x100[0] ||
        s.load_x100[1] != g_prev.load_x100[1] ||
        s.load_x100[2] != g_prev.load_x100[2] || s.cpu_pct != g_prev.cpu_pct ||
        s.mem_pct != g_prev.mem_pct || s.disk_pct != g_prev.disk_pct)
      d |= DIRTY_SECONDARY;
  }

  if (memcmp(s.ifaces, g_prev.ifaces, sizeof(s.ifaces)) != 0 || roll_tick)
    d |= DIRTY_NET;

  if (memcmp(s.services, g_prev.services, sizeof(s.services)) != 0)
    d |= DIRTY_SERVICES;

  return d;
}

static void paintHeader(TFT_eSPI *tft, const StatusSnap &s,
                        const StatusLayout &L) {
  char host[25], date[21], tim[13];
  memcpy(host, s.hostname, 24);
  host[24] = 0;
  memcpy(date, s.date, 20);
  date[20] = 0;
  memcpy(tim, s.time, 12);
  tim[12] = 0;

  clearRect(tft, 0, 0, 320, L.header_h);
  drawText1(tft, 4, 4, host[0] ? host : "host",
            s.style.host_c ? s.style.host_c : COL_WHITE);

  tft->setTextDatum(TR_DATUM);
  tft->setTextFont(1);
  const char *t = tim[0] ? tim : "--:--:--";
  const char *d = date[0] ? date : "--/--/----";
  int tw = tft->textWidth(t, 1);
  tft->setTextColor(s.style.time_c ? s.style.time_c : COL_WHITE, g_bg);
  tft->drawString(t, 316, 4, 1);
  tft->setTextColor(s.style.date_c ? s.style.date_c : g_label, g_bg);
  tft->drawString(d, 316 - tw - 6, 4, 1);
  tft->setTextDatum(TL_DATUM);
}

static void paintHero(TFT_eSPI *tft, const StatusSnap &s, const StatusLayout &L) {
  char line[32];
  char a[12], b[12];
  const int top = L.y_hero_label;
  const int bot = L.secondary ? L.y_secondary : L.y_net0;
  clearRect(tft, 0, top, 320, bot - top);

  // Font1 labels, placed below the header strip so they are not cropped.
  drawText1(tft, 6, L.y_hero_label, "CPU", g_label);
  drawText1(tft, 112, L.y_hero_label, "MEM", g_label);
  drawText1(tft, 218, L.y_hero_label, "DISK", g_label);

  if (s.flags & ST_F_HAS_CPU) {
    snprintf(line, sizeof(line), "%u%%", (unsigned)s.cpu_pct);
    drawBig(tft, 6, L.y_hero_pct, line, heroMeterColor(s, 0, s.cpu_pct));
  } else {
    drawBig(tft, 6, L.y_hero_pct, "--", COL_MUTED);
  }
  if (s.flags & ST_F_HAS_TEMP) {
    snprintf(line, sizeof(line), "TEMP: %.0fC", s.cpu_temp_c10 / 10.0f);
    drawText1(tft, 6, L.y_hero_sub, line, g_label);
  } else {
    drawText1(tft, 6, L.y_hero_sub, "TEMP: --", g_label);
  }

  if (s.flags & ST_F_HAS_MEM) {
    snprintf(line, sizeof(line), "%u%%", (unsigned)s.mem_pct);
    drawBig(tft, 112, L.y_hero_pct, line, heroMeterColor(s, 1, s.mem_pct));
    fmtGb(a, sizeof(a), s.mem_used_mb);
    fmtGb(b, sizeof(b), s.mem_total_mb);
    snprintf(line, sizeof(line), "%s/%s GB", a, b);
    drawText1(tft, 112, L.y_hero_sub, line, g_label);
  } else {
    drawBig(tft, 112, L.y_hero_pct, "--", COL_MUTED);
    drawText1(tft, 112, L.y_hero_sub, "--/-- GB", g_label);
  }

  if (s.flags & ST_F_HAS_DISK) {
    snprintf(line, sizeof(line), "%u%%", (unsigned)s.disk_pct);
    drawBig(tft, 218, L.y_hero_pct, line, heroMeterColor(s, 2, s.disk_pct));
    fmtGb(a, sizeof(a), s.disk_used_mb);
    fmtGb(b, sizeof(b), s.disk_total_mb);
    snprintf(line, sizeof(line), "%s/%s GB", a, b);
    drawText1(tft, 218, L.y_hero_sub, line, g_label);
  } else {
    drawBig(tft, 218, L.y_hero_pct, "--", COL_MUTED);
    drawText1(tft, 218, L.y_hero_sub, "--/-- GB", g_label);
  }
}

static void paintOneSecondary(TFT_eSPI *tft, const StatusSnap &s, uint8_t id,
                              uint16_t color, int x_label, int x_val, int y) {
  if (id == SEC_NONE)
    return;
  const char *lab = secLabel(id);
  drawText1(tft, x_label, y, lab, g_label);
  char val[40];
  fmtSecValue(val, sizeof(val), s, id);
  drawText1(tft, x_val, y, val, color ? color : COL_WHITE);
}

static void paintSecondary(TFT_eSPI *tft, const StatusSnap &s,
                           const StatusLayout &L) {
  if (!L.secondary)
    return;
  clearRect(tft, 0, L.y_secondary - 1, 320, STATUS_SECONDARY_H);
  paintOneSecondary(tft, s, s.style.sec_left, s.style.sec_left_c, 6, 54,
                    L.y_secondary);
  paintOneSecondary(tft, s, s.style.sec_right, s.style.sec_right_c, 168, 210,
                    L.y_secondary);
}

static void paintNet(TFT_eSPI *tft, const StatusSnap &s, const StatusLayout &L,
                     bool force) {
  const int n = L.iface_n;
  const int prev_n = g_prev_iface_n;
  if (force || n != prev_n) {
    const int wipe = (n > prev_n ? n : prev_n);
    if (wipe > 0 || g_prev_layout.y_net0 != L.y_net0)
      clearRect(tft, 0, (L.y_net0 < g_prev_layout.y_net0 ? L.y_net0
                                                         : g_prev_layout.y_net0) -
                            1,
                320, (wipe > 0 ? wipe : 1) * STATUS_IFACE_STEP + 4);
  }
  if (n == 0) {
    g_prev_iface_n = 0;
    return;
  }

  char rx[12], tx[12], name[IFACE_NAME_SHOW + 1], ip[IFACE_IP_SHOW + 1];
  const int x_name = 6;
  const int x_ip = 6 + (int)IFACE_NAME_SHOW * 6 + 8;
  const int x_rx = 200;
  const int x_rx_v = 218;
  const int x_tx = 258;
  const int x_tx_v = 276;
  const uint16_t name_off = (g_roll_phase == ROLL_NAME) ? g_roll_off : 0;
  const uint16_t ip_off = (g_roll_phase == ROLL_IP) ? g_roll_off : 0;

  for (int i = 0; i < n; i++) {
    const StatusIface &cur = s.ifaces[i];
    const int y = L.y_net0 + i * STATUS_IFACE_STEP;
    clearRect(tft, 0, y - 1, 320, STATUS_IFACE_STEP);
    if (!cur.name[0])
      continue;
    rollWindow(name, IFACE_NAME_SHOW, cur.name, IFACE_NAME_WIRE, name_off);
    if (cur.ip[0])
      rollWindow(ip, IFACE_IP_SHOW, cur.ip, IFACE_IP_WIRE, ip_off);
    else {
      ip[0] = '-';
      ip[1] = 0;
    }
    fmtRate(rx, sizeof(rx), cur.rx_bps);
    fmtRate(tx, sizeof(tx), cur.tx_bps);
    drawText1(tft, x_name, y, name, g_label);
    drawText1(tft, x_ip, y, ip, COL_WHITE);
    drawText1(tft, x_rx, y, "RX:", COL_RX);
    drawText1(tft, x_rx_v, y, rx, COL_WHITE);
    drawText1(tft, x_tx, y, "TX:", COL_TX);
    drawText1(tft, x_tx_v, y, tx, COL_WHITE);
  }
  g_prev_iface_n = n;
}

static void paintServices(TFT_eSPI *tft, const StatusSnap &s,
                          const StatusLayout &L) {
  const int y0 = L.y_services;
  const int n = statusSvcCount(s);
  const int old_y = g_prev_svc_y >= 0 ? g_prev_svc_y : y0;
  const int clear_y = old_y < y0 ? old_y : y0;
  clearRect(tft, 0, clear_y - 1, 320, 240 - clear_y);

  if (n == 0) {
    g_prev_svc_y = y0;
    g_prev_svc_n = 0;
    return;
  }

  drawText1(tft, 6, y0, "SERVICES", g_label);
  int x = 6;
  int y = y0 + 12;
  const int max_x = 314;
  const int row_h = 11;
  for (int i = 0; i < n; i++) {
    char name[STATUS_SVC_NAME + 1];
    memcpy(name, s.services[i].name, STATUS_SVC_NAME);
    name[STATUS_SVC_NAME] = 0;
    if (!name[0])
      continue;
    const int w = (int)strlen(name) * 6;
    if (x > 6 && x + w > max_x) {
      x = 6;
      y += row_h;
    }
    if (y + 8 > 238)
      break;
    drawText1(tft, x, y, name, svcColor(s.style, s.services[i].status));
    x += w + 8;
  }
  g_prev_svc_y = y0;
  g_prev_svc_n = n;
}

void paintStatusGui(TFT_eSPI *tft, const StatusSnap &s, bool force_full) {
  if (!tft)
    return;

  applyTheme(s.style);

  const uint32_t now = millis();
  const bool roll_tick = advanceRoll(now, s);
  const StatusLayout L = statusLayoutOf(s);
  const bool force = force_full || !g_chrome;
  const uint8_t dirty = computeDirty(s, L, force, roll_tick);

  if (force) {
    tft->fillScreen(g_bg);
    g_chrome = true;
  }

  if (dirty & DIRTY_HEADER)
    paintHeader(tft, s, L);
  if (dirty & DIRTY_HERO)
    paintHero(tft, s, L);
  if (dirty & DIRTY_SECONDARY)
    paintSecondary(tft, s, L);
  if (dirty & DIRTY_NET)
    paintNet(tft, s, L, force || (dirty == DIRTY_ALL));
  if (dirty & DIRTY_SERVICES)
    paintServices(tft, s, L);

  g_prev = s;
  g_prev_layout = L;
  g_have_prev = true;
}
