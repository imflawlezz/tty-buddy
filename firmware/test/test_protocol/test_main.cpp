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

void test_status_snap_sizes(void) {
  TEST_ASSERT_EQUAL(45, (int)sizeof(StatusStyle));
  TEST_ASSERT_EQUAL(4451, (int)sizeof(StatusSnap));
  TEST_ASSERT_TRUE(sizeof(StatusSnap) <= (size_t)TERM_PAYLOAD);
  TEST_ASSERT_EQUAL(4770, TERM_PAYLOAD);
  TEST_ASSERT_EQUAL(53, TERM_COLS);
  TEST_ASSERT_EQUAL(30, TERM_ROWS);
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

int main(int argc, char **argv) {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_empty_and_known);
  RUN_TEST(test_crc16_chained_matches_concat);
  RUN_TEST(test_status_snap_sizes);
  RUN_TEST(test_status_snap_valid_and_clear);
  RUN_TEST(test_status_layout_with_ifaces_and_secondary);
  RUN_TEST(test_status_layout_without_secondary);
  return UNITY_END();
}
