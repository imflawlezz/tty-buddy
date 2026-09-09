#include <unity.h>
#include <string.h>

#include "protocol.h"
#include "status_snap.h"

void test_crc16_empty_and_known(void) {
  const uint8_t empty[] = {0};
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16_ccitt(empty, 0));
  const uint8_t msg[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt(msg, sizeof(msg)));
}

void test_crc16_chained_matches_concat(void) {
  const uint8_t a[] = {1, 2, 3, 4};
  const uint8_t b[] = {5, 6, 7, 8};
  uint8_t both[8];
  memcpy(both, a, 4);
  memcpy(both + 4, b, 4);
  uint16_t chained = crc16_ccitt(a, 4);
  chained = crc16_ccitt(b, 4, chained);
  TEST_ASSERT_EQUAL_HEX16(crc16_ccitt(both, 8), chained);
}

void test_wire_constants_match_host(void) {
  // Keep in lockstep with daemon/src/protocol.rs.
  TEST_ASSERT_EQUAL_HEX8(0xAA, FRAME_M0);
  TEST_ASSERT_EQUAL_HEX8(0x55, FRAME_M1);
  TEST_ASSERT_EQUAL_HEX8(0xA5, FRAME_M2);
  TEST_ASSERT_EQUAL_HEX8(0x5A, FRAME_M3);
  TEST_ASSERT_EQUAL_HEX8(0x06, FRAME_ACK);
  TEST_ASSERT_EQUAL_HEX8(0x15, FRAME_NAK);
  TEST_ASSERT_EQUAL_HEX8(0x12, DEV_MODE_TOGGLE);
  TEST_ASSERT_EQUAL_HEX8(0x13, DEV_OSD_BRIGHT);
  TEST_ASSERT_EQUAL_HEX8(0x14, DEV_OSD_SLEEP);
  TEST_ASSERT_EQUAL_HEX8(0x01, CURSOR_VISIBLE);
  TEST_ASSERT_EQUAL_HEX8(0x02, CURSOR_ON);
  TEST_ASSERT_EQUAL_HEX8(0x04, FLAG_ACTIVITY);
  TEST_ASSERT_EQUAL_HEX8(0x10, FLAG_STYLE);
  TEST_ASSERT_EQUAL_HEX8(0x20, FLAG_STATUS);
  TEST_ASSERT_EQUAL_HEX8(0x80, FLAG_BYE);
  TEST_ASSERT_EQUAL(53, TERM_COLS);
  TEST_ASSERT_EQUAL(30, TERM_ROWS);
  TEST_ASSERT_EQUAL(4770, TERM_PAYLOAD);
  TEST_ASSERT_EQUAL(4780, 4 + 4 + TERM_PAYLOAD + 2);
  TEST_ASSERT_EQUAL(13, STATUS_VER);
  TEST_ASSERT_EQUAL(60, (int)sizeof(StatusStyle));
  TEST_ASSERT_EQUAL(4466, (int)sizeof(StatusSnap));
  TEST_ASSERT_EQUAL(16, STATUS_IFACE_COUNT);
  TEST_ASSERT_EQUAL(80, STATUS_SVC_COUNT);
  TEST_ASSERT_EQUAL(40, (int)STATUS_SVC_NAME);
  TEST_ASSERT_EQUAL_HEX8(0x02, ST_F_HAS_TEMP);
  TEST_ASSERT_EQUAL_HEX8(0x20, ST_F_HAS_CPU);
  TEST_ASSERT_EQUAL_HEX8(0x40, ST_F_HAS_MEM);
  TEST_ASSERT_EQUAL_HEX8(0x80, ST_F_HAS_DISK);
  TEST_ASSERT_EQUAL(0, ST_SVC_FAILED);
  TEST_ASSERT_EQUAL(1, ST_SVC_ACTIVE);
  TEST_ASSERT_EQUAL(2, ST_SVC_DEACTIVATING);
  TEST_ASSERT_EQUAL(3, ST_SVC_ACTIVATING);
  TEST_ASSERT_EQUAL(4, ST_SVC_INACTIVE);
  TEST_ASSERT_EQUAL(5, ST_SVC_MAINTENANCE);
  TEST_ASSERT_EQUAL(6, ST_SVC_RELOADING);
  TEST_ASSERT_EQUAL_HEX8(0xFF, SEC_NONE);
  TEST_ASSERT_EQUAL(1, SEC_UPTIME);
  TEST_ASSERT_EQUAL(2, SEC_SWAP);
  TEST_ASSERT_EQUAL(3, SEC_LOAD);
  TEST_ASSERT_EQUAL(1, AL_CPU);
  TEST_ASSERT_EQUAL(2, AL_MEM);
  TEST_ASSERT_EQUAL(4, AL_DISK);
  TEST_ASSERT_EQUAL(8, AL_TEMP);
  TEST_ASSERT_EQUAL(16, AL_SVC_FAILED);
  TEST_ASSERT_EQUAL(32, AL_SVC_INACTIVE);
  TEST_ASSERT_EQUAL(1, OSD_F_DISMISS_ON_TAP);
  TEST_ASSERT_EQUAL(2, OSD_F_AUTO_BRIGHT);
  TEST_ASSERT_EQUAL(4, OSD_F_WAKE_ON_ALERT);
}

void test_status_snap_sizes(void) {
  TEST_ASSERT_EQUAL(60, (int)sizeof(StatusStyle));
  TEST_ASSERT_EQUAL(4466, (int)sizeof(StatusSnap));
  TEST_ASSERT_TRUE(sizeof(StatusSnap) <= (size_t)TERM_PAYLOAD);
  TEST_ASSERT_EQUAL(4770, TERM_PAYLOAD);
  TEST_ASSERT_EQUAL(53, TERM_COLS);
  TEST_ASSERT_EQUAL(30, TERM_ROWS);
}

void fill_golden_style(StatusStyle &st) {
  memset(&st, 0, sizeof(st));
  st.label_c = 0x1111;
  st.bg_c = 0x2222;
  st.host_c = 0x3333;
  st.date_c = 0x4444;
  st.time_c = 0x5555;
  st.level_ok = 0x6666;
  st.level_warn = 0x7777;
  st.level_crit = 0x8888;
  st.hero_cpu_c = 0x9999;
  st.hero_mem_c = 0xAAAA;
  st.hero_disk_c = 0xBBBB;
  st.meter_mode = METER_ON;
  st.warn_at = 60;
  st.crit_at = 90;
  st.sec_left = SEC_UPTIME;
  st.sec_right = SEC_LOAD;
  st.sec_left_c = 0xCCCC;
  st.sec_right_c = 0xDDDD;
  st.svc_active = 0x0101;
  st.svc_failed = 0x0202;
  st.svc_deactivating = 0x0303;
  st.svc_activating = 0x0404;
  st.svc_reloading = 0x0505;
  st.svc_inactive = 0x0606;
  st.svc_maintenance = 0x0707;
  st.alert_bg_c = 0x0808;
  st.alert_fg_c = 0x0909;
  st.alert_hold_sec = 30;
  st.alert_mask = 0x3F;
  st.alert_temp_c = 80;
  st.osd_default_bright_pct = 4;
  st.osd_sleep_timeout_s = 4;
  st.osd_flags = OSD_F_DISMISS_ON_TAP | OSD_F_WAKE_ON_ALERT;
  st.osd_auto_day_pct = 5;
  st.osd_auto_night_pct = 2;
  st.osd_auto_day_hour = 7;
  st.osd_auto_night_hour = 21;
}

void test_status_style_pack_golden(void) {
  // Lockstep with daemon `status_style_pack_golden`.
  static const uint8_t GOLDEN[60] = {
      0x11, 0x11, 0x22, 0x22, 0x33, 0x33, 0x44, 0x44, 0x55, 0x55, 0x66, 0x66,
      0x77, 0x77, 0x88, 0x88, 0x99, 0x99, 0xAA, 0xAA, 0xBB, 0xBB, 0x01, 0x3C,
      0x5A, 0x01, 0x03, 0xCC, 0xCC, 0xDD, 0xDD, 0x01, 0x01, 0x02, 0x02, 0x03,
      0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08, 0x08, 0x09,
      0x09, 0x1E, 0x3F, 0x50, 0x04, 0x04, 0x00, 0x05, 0x05, 0x02, 0x07, 0x15};
  StatusStyle st{};
  fill_golden_style(st);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(GOLDEN, (const uint8_t *)&st, 60);
}

void test_status_snap_header_golden(void) {
  // Lockstep with daemon `status_snap_header_golden`.
  StatusSnap s{};
  statusSnapClear(s);
  fill_golden_style(s.style);
  s.flags = ST_F_HAS_CPU | ST_F_HAS_MEM | ST_F_HAS_DISK;
  strncpy(s.hostname, "buddy", sizeof(s.hostname));
  strncpy(s.date, "09-09-2026", sizeof(s.date));
  strncpy(s.time, "12:00:00", sizeof(s.time));
  s.load_x100[0] = 100;
  s.load_x100[1] = 200;
  s.load_x100[2] = 300;
  s.uptime_sec = 3600;
  s.cpu_pct = 11;
  s.mem_pct = 22;
  s.disk_pct = 33;
  s.swap_pct = 44;
  s.mem_used_mb = 1024;
  s.mem_total_mb = 2048;
  s.disk_used_mb = 4096;
  s.disk_total_mb = 8192;
  s.swap_used_mb = 1;
  s.swap_total_mb = 2;
  s.cpu_temp_c10 = 425;
  strncpy(s.ifaces[0].name, "eth0", sizeof(s.ifaces[0].name));
  strncpy(s.ifaces[0].ip, "10.0.0.1", sizeof(s.ifaces[0].ip));
  s.ifaces[0].rx_bps = 1000;
  s.ifaces[0].tx_bps = 2000;
  strncpy(s.services[0].name, "ssh", sizeof(s.services[0].name));
  s.services[0].status = ST_SVC_ACTIVE;

  TEST_ASSERT_TRUE(statusSnapValid(s));
  TEST_ASSERT_EQUAL(STATUS_VER, s.ver);
  TEST_ASSERT_EQUAL_STRING("buddy", s.hostname);
  TEST_ASSERT_EQUAL_STRING("eth0", s.ifaces[0].name);
  TEST_ASSERT_EQUAL_STRING("10.0.0.1", s.ifaces[0].ip);
  TEST_ASSERT_EQUAL_UINT32(1000, s.ifaces[0].rx_bps);
  TEST_ASSERT_EQUAL_UINT32(2000, s.ifaces[0].tx_bps);
  TEST_ASSERT_EQUAL_STRING("ssh", s.services[0].name);
  TEST_ASSERT_EQUAL(ST_SVC_ACTIVE, s.services[0].status);

  const uint8_t *raw = (const uint8_t *)&s;
  TEST_ASSERT_EQUAL('T', raw[0]);
  TEST_ASSERT_EQUAL('B', raw[1]);
  TEST_ASSERT_EQUAL('S', raw[2]);
  TEST_ASSERT_EQUAL('T', raw[3]);
  TEST_ASSERT_EQUAL(STATUS_VER, raw[4]);
  TEST_ASSERT_EQUAL(ST_F_HAS_CPU | ST_F_HAS_MEM | ST_F_HAS_DISK, raw[5]);

  static const uint8_t STYLE_GOLDEN[60] = {
      0x11, 0x11, 0x22, 0x22, 0x33, 0x33, 0x44, 0x44, 0x55, 0x55, 0x66, 0x66,
      0x77, 0x77, 0x88, 0x88, 0x99, 0x99, 0xAA, 0xAA, 0xBB, 0xBB, 0x01, 0x3C,
      0x5A, 0x01, 0x03, 0xCC, 0xCC, 0xDD, 0xDD, 0x01, 0x01, 0x02, 0x02, 0x03,
      0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08, 0x08, 0x09,
      0x09, 0x1E, 0x3F, 0x50, 0x04, 0x04, 0x00, 0x05, 0x05, 0x02, 0x07, 0x15};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(STYLE_GOLDEN, raw + 6, 60);
  TEST_ASSERT_EQUAL_STRING("buddy", (const char *)(raw + 6 + 60));
}

void test_status_frame_crc_golden(void) {
  // Lockstep with daemon `status_frame_crc_golden`.
  uint8_t hdr[4] = {1, 0, 0, (uint8_t)(FLAG_STATUS | FLAG_STYLE)};
  uint8_t payload[TERM_PAYLOAD];
  memset(payload, 0, sizeof(payload));
  uint16_t crc = crc16_ccitt(hdr, 4);
  crc = crc16_ccitt(payload, TERM_PAYLOAD, crc);
  TEST_ASSERT_EQUAL_HEX16(0x9D4D, crc);
}

void test_status_snap_valid_and_clear(void) {
  StatusSnap s{};
  TEST_ASSERT_FALSE(statusSnapValid(s));
  statusSnapClear(s);
  TEST_ASSERT_TRUE(statusSnapValid(s));
  TEST_ASSERT_EQUAL(STATUS_VER, s.ver);
  TEST_ASSERT_EQUAL(255, s.cpu_pct);
  TEST_ASSERT_EQUAL(SEC_NONE, s.style.sec_left);
}

void test_status_snap_clear_osd_defaults(void) {
  StatusSnap s{};
  statusSnapClear(s);
  TEST_ASSERT_EQUAL(4, s.style.osd_default_bright_pct);
  TEST_ASSERT_EQUAL(4, s.style.osd_sleep_timeout_s);
  TEST_ASSERT_EQUAL(OSD_F_DISMISS_ON_TAP | OSD_F_WAKE_ON_ALERT, s.style.osd_flags);
  TEST_ASSERT_EQUAL(5, s.style.osd_auto_day_pct);
  TEST_ASSERT_EQUAL(2, s.style.osd_auto_night_pct);
  TEST_ASSERT_EQUAL(7, s.style.osd_auto_day_hour);
  TEST_ASSERT_EQUAL(21, s.style.osd_auto_night_hour);
}

void test_status_layout_with_ifaces_and_secondary(void) {
  StatusSnap s{};
  statusSnapClear(s);
  s.style.sec_left = SEC_UPTIME;
  s.style.sec_right = SEC_LOAD;
  memcpy(s.ifaces[0].name, "eth0", 5);
  memcpy(s.ifaces[1].name, "wlan0", 6);

  StatusLayout L = statusLayoutOf(s);
  TEST_ASSERT_TRUE(L.secondary);
  TEST_ASSERT_EQUAL(2, L.iface_n);
  TEST_ASSERT_TRUE(L.y_secondary >= 0);
  TEST_ASSERT_TRUE(L.y_net0 > L.y_secondary);
  TEST_ASSERT_TRUE(L.y_services > L.y_net0);
}

void test_status_layout_without_secondary(void) {
  StatusSnap s{};
  statusSnapClear(s);
  StatusLayout L = statusLayoutOf(s);
  TEST_ASSERT_FALSE(L.secondary);
  TEST_ASSERT_EQUAL(-1, L.y_secondary);
  TEST_ASSERT_EQUAL(0, L.iface_n);
}

void test_status_build_alert_combined(void) {
  StatusSnap s{};
  statusSnapClear(s);
  s.style.alert_mask = AL_CPU | AL_MEM | AL_TEMP | AL_SVC_FAILED;
  s.style.crit_at = 90;
  s.style.alert_temp_c = 80;
  s.flags = ST_F_HAS_CPU | ST_F_HAS_MEM | ST_F_HAS_TEMP;
  s.cpu_pct = 95;
  s.mem_pct = 91;
  s.cpu_temp_c10 = 850;
  memcpy(s.services[0].name, "ssh", 4);
  s.services[0].status = ST_SVC_FAILED;
  memcpy(s.services[1].name, "cron", 5);
  s.services[1].status = ST_SVC_ACTIVE;

  char buf[STATUS_ALERT_CHARS + 1];
  statusBuildAlert(s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("CPU high ! MEM high ! TEMP 85C ! ssh down", buf);
}

void test_status_build_alert_svc_groups(void) {
  StatusSnap s{};
  statusSnapClear(s);
  s.style.alert_mask = AL_CPU | AL_SVC_FAILED | AL_SVC_INACTIVE;
  s.style.crit_at = 90;
  s.flags = ST_F_HAS_CPU;
  s.cpu_pct = 95;
  memcpy(s.services[0].name, "tty-buddy", 10);
  s.services[0].status = ST_SVC_FAILED;
  memcpy(s.services[1].name, "docker", 7);
  s.services[1].status = ST_SVC_FAILED;
  memcpy(s.services[2].name, "cron", 5);
  s.services[2].status = ST_SVC_INACTIVE;

  char buf[STATUS_ALERT_CHARS + 1];
  statusBuildAlert(s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("CPU high ! tty-buddy, docker down ! cron inactive",
                           buf);
}

void test_status_build_alert_svc_inactive(void) {
  StatusSnap s{};
  statusSnapClear(s);
  s.style.alert_mask = AL_SVC_INACTIVE;
  memcpy(s.services[0].name, "cron", 5);
  s.services[0].status = ST_SVC_INACTIVE;
  memcpy(s.services[1].name, "ssh", 4);
  s.services[1].status = ST_SVC_ACTIVE;

  char buf[STATUS_ALERT_CHARS + 1];
  statusBuildAlert(s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("cron inactive", buf);
}

void test_status_build_alert_mask_off(void) {
  StatusSnap s{};
  statusSnapClear(s);
  s.style.alert_mask = 0;
  s.flags = ST_F_HAS_CPU;
  s.cpu_pct = 99;
  char buf[32];
  statusBuildAlert(s, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("", buf);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_empty_and_known);
  RUN_TEST(test_crc16_chained_matches_concat);
  RUN_TEST(test_wire_constants_match_host);
  RUN_TEST(test_status_snap_sizes);
  RUN_TEST(test_status_style_pack_golden);
  RUN_TEST(test_status_snap_header_golden);
  RUN_TEST(test_status_frame_crc_golden);
  RUN_TEST(test_status_snap_valid_and_clear);
  RUN_TEST(test_status_snap_clear_osd_defaults);
  RUN_TEST(test_status_layout_with_ifaces_and_secondary);
  RUN_TEST(test_status_layout_without_secondary);
  RUN_TEST(test_status_build_alert_combined);
  RUN_TEST(test_status_build_alert_svc_groups);
  RUN_TEST(test_status_build_alert_svc_inactive);
  RUN_TEST(test_status_build_alert_mask_off);
  return UNITY_END();
}
