#pragma once

// Wire StatusSnap (little-endian); no TFT/Arduino so host unit tests can include it.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "protocol.h"

static constexpr char STATUS_MAGIC0 = 'T';
static constexpr char STATUS_MAGIC1 = 'B';
static constexpr char STATUS_MAGIC2 = 'S';
static constexpr char STATUS_MAGIC3 = 'T';
static constexpr uint8_t STATUS_VER = 11;

static constexpr uint8_t ST_F_HAS_TEMP = 0x02;
static constexpr uint8_t ST_F_HAS_CPU = 0x20;
static constexpr uint8_t ST_F_HAS_MEM = 0x40;
static constexpr uint8_t ST_F_HAS_DISK = 0x80;

static constexpr uint8_t ST_SVC_FAILED = 0;
static constexpr uint8_t ST_SVC_ACTIVE = 1;
static constexpr uint8_t ST_SVC_DEACTIVATING = 2;
static constexpr uint8_t ST_SVC_ACTIVATING = 3;
static constexpr uint8_t ST_SVC_INACTIVE = 4;
static constexpr uint8_t ST_SVC_MAINTENANCE = 5;
static constexpr uint8_t ST_SVC_RELOADING = 6;

static constexpr uint8_t SEC_NONE = 0xFF;
static constexpr uint8_t SEC_UPTIME = 1;
static constexpr uint8_t SEC_SWAP = 2;
static constexpr uint8_t SEC_LOAD = 3;

static constexpr uint8_t METER_OFF = 0;
static constexpr uint8_t METER_ON = 1;

static constexpr size_t IFACE_NAME_WIRE = 16;
static constexpr size_t IFACE_NAME_SHOW = 10;
static constexpr size_t IFACE_IP_WIRE = 40;
static constexpr size_t IFACE_IP_SHOW = 20;
static constexpr int STATUS_IFACE_COUNT = 16;
static constexpr int STATUS_SVC_COUNT = 80;
static constexpr size_t STATUS_SVC_NAME = 40;

static constexpr int STATUS_HEADER_H = 16;
static constexpr int STATUS_HERO_H = 52;
static constexpr int STATUS_SECONDARY_H = 14;
static constexpr int STATUS_IFACE_STEP = 14;

struct __attribute__((packed)) StatusStyle {
  uint16_t label_c;
  uint16_t bg_c;
  uint16_t host_c;
  uint16_t date_c;
  uint16_t time_c;
  uint16_t level_ok;
  uint16_t level_warn;
  uint16_t level_crit;
  uint16_t hero_cpu_c;
  uint16_t hero_mem_c;
  uint16_t hero_disk_c;
  uint8_t meter_mode;
  uint8_t warn_at;
  uint8_t crit_at;
  uint8_t sec_left;
  uint8_t sec_right;
  uint16_t sec_left_c;
  uint16_t sec_right_c;
  uint16_t svc_active;
  uint16_t svc_failed;
  uint16_t svc_deactivating;
  uint16_t svc_activating;
  uint16_t svc_reloading;
  uint16_t svc_inactive;
  uint16_t svc_maintenance;
};

struct __attribute__((packed)) StatusIface {
  char name[IFACE_NAME_WIRE];
  char ip[IFACE_IP_WIRE];
  uint32_t rx_bps;
  uint32_t tx_bps;
};

struct __attribute__((packed)) StatusSvc {
  char name[STATUS_SVC_NAME];
  uint8_t status;
};

struct __attribute__((packed)) StatusSnap {
  char magic[4];
  uint8_t ver;
  uint8_t flags;
  StatusStyle style;
  char hostname[24];
  char date[20];
  char time[12];
  uint16_t load_x100[3];
  uint32_t uptime_sec;
  uint8_t cpu_pct;
  uint8_t mem_pct;
  uint8_t disk_pct;
  uint8_t swap_pct;
  uint32_t mem_used_mb;
  uint32_t mem_total_mb;
  uint32_t disk_used_mb;
  uint32_t disk_total_mb;
  uint32_t swap_used_mb;
  uint32_t swap_total_mb;
  int16_t cpu_temp_c10;
  StatusIface ifaces[STATUS_IFACE_COUNT];
  StatusSvc services[STATUS_SVC_COUNT];
};

static_assert(sizeof(StatusStyle) == 45, "StatusStyle size");
static_assert(sizeof(StatusSnap) == 4451, "StatusSnap wire size");
static_assert(sizeof(StatusSnap) <= TERM_PAYLOAD, "StatusSnap must fit TERM_PAYLOAD");

inline int statusIfaceCount(const StatusSnap &s) {
  int n = 0;
  for (int i = 0; i < STATUS_IFACE_COUNT; i++) {
    if (s.ifaces[i].name[0])
      n++;
    else
      break;
  }
  return n;
}

inline int statusSvcCount(const StatusSnap &s) {
  int n = 0;
  for (int i = 0; i < STATUS_SVC_COUNT; i++) {
    if (s.services[i].name[0])
      n++;
    else
      break;
  }
  return n;
}

inline bool statusSecondaryVisible(const StatusSnap &s) {
  return s.style.sec_left != SEC_NONE || s.style.sec_right != SEC_NONE;
}

struct StatusLayout {
  int header_h;
  int y_hero_label;
  int y_hero_pct;
  int y_hero_sub;
  int y_secondary;
  int y_net0;
  int y_services;
  bool secondary;
  int iface_n;
};

inline StatusLayout statusLayoutOf(const StatusSnap &s) {
  StatusLayout L{};
  L.header_h = STATUS_HEADER_H;
  // Font4 (~28px) must clear fully before the sub-line or glyphs ghost into it.
  L.y_hero_label = STATUS_HEADER_H + 2;
  L.y_hero_pct = L.y_hero_label + 10;
  L.y_hero_sub = L.y_hero_pct + 28;
  int y = L.y_hero_sub + 12;
  L.secondary = statusSecondaryVisible(s);
  if (L.secondary) {
    L.y_secondary = y;
    y += STATUS_SECONDARY_H;
  } else {
    L.y_secondary = -1;
  }
  L.iface_n = statusIfaceCount(s);
  L.y_net0 = y;
  y += L.iface_n * STATUS_IFACE_STEP;
  if (L.iface_n > 0)
    y += 2;
  L.y_services = y;
  return L;
}

inline bool statusSnapValid(const StatusSnap &s) {
  return s.magic[0] == STATUS_MAGIC0 && s.magic[1] == STATUS_MAGIC1 &&
         s.magic[2] == STATUS_MAGIC2 && s.magic[3] == STATUS_MAGIC3 &&
         s.ver == STATUS_VER;
}

inline void statusSnapClear(StatusSnap &s) {
  memset(&s, 0, sizeof(s));
  s.magic[0] = STATUS_MAGIC0;
  s.magic[1] = STATUS_MAGIC1;
  s.magic[2] = STATUS_MAGIC2;
  s.magic[3] = STATUS_MAGIC3;
  s.ver = STATUS_VER;
  s.cpu_pct = 255;
  s.mem_pct = 255;
  s.disk_pct = 255;
  s.swap_pct = 255;
  s.cpu_temp_c10 = 0x7FFF;
  s.style.sec_left = SEC_NONE;
  s.style.sec_right = SEC_NONE;
  s.style.meter_mode = METER_ON;
  s.style.warn_at = 60;
  s.style.crit_at = 90;
  s.style.label_c = 0x8C51;
  s.style.bg_c = 0;
}
