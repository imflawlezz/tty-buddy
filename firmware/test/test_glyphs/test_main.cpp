#include <unity.h>

#include "glyph_font.h"

void test_german_letters(void) {
  uint8_t rows[8];
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x00E4, rows)); // ä
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x00F6, rows)); // ö
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x00FC, rows)); // ü
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x00DF, rows)); // ß
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x00C4, rows)); // Ä
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x00D6, rows)); // Ö
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x00DC, rows)); // Ü
}

void test_polish_letters(void) {
  uint8_t rows[8];
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x0105, rows)); // ą
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x0107, rows)); // ć
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x0119, rows)); // ę
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x0142, rows)); // ł
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x0144, rows)); // ń
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x00F3, rows)); // ó
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x015B, rows)); // ś
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x017A, rows)); // ź
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x017C, rows)); // ż
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x0104, rows)); // Ą
  TEST_ASSERT_TRUE(glyphUnicodeRows(0x017B, rows)); // Ż
}

void test_unsupported_rejected(void) {
  uint8_t rows[8];
  TEST_ASSERT_FALSE(glyphUnicodeRows('A', rows));
  TEST_ASSERT_FALSE(glyphUnicodeRows(0x0410, rows)); // Cyrillic А
  TEST_ASSERT_FALSE(glyphUnicodeRows(0x0391, rows)); // Greek Α
  TEST_ASSERT_FALSE(glyphUnicodeRows(0x00E9, rows)); // é (not in PL/DE set)
}

void setUp(void) {}
void tearDown(void) {}

int runUnityTests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_german_letters);
  RUN_TEST(test_polish_letters);
  RUN_TEST(test_unsupported_rejected);
  return UNITY_END();
}

int main(void) { return runUnityTests(); }
