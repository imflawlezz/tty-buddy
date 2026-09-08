#pragma once

#include <cstddef>
#include <cstdint>

// Frame: AA 55 A5 5A | seq | cx | cy | flags | payload | crc16_be
// flags: bit0 cursor, bit1 blink, bit5 status GUI snapshot, bit7 bye
static constexpr uint8_t FRAME_M0 = 0xAA;
static constexpr uint8_t FRAME_M1 = 0x55;
static constexpr uint8_t FRAME_M2 = 0xA5;
static constexpr uint8_t FRAME_M3 = 0x5A;
static constexpr uint8_t FRAME_ACK = 0x06;
static constexpr uint8_t FRAME_NAK = 0x15;
// Device → host: long-press toggles terminal ↔ status (single byte).
static constexpr uint8_t DEV_MODE_TOGGLE = 0x12;
static constexpr uint8_t CURSOR_VISIBLE = 0x01;
static constexpr uint8_t CURSOR_ON = 0x02;
static constexpr uint8_t FLAG_STATUS = 0x20;
static constexpr uint8_t FLAG_BYE = 0x80;

static constexpr int TERM_CELL_W = 6;
static constexpr int TERM_CELL_H = 8;
static constexpr int TERM_COLS = 320 / TERM_CELL_W; // 53
static constexpr int TERM_ROWS = 240 / TERM_CELL_H; // 30
static constexpr int TERM_CELLS = TERM_COLS * TERM_ROWS;
static constexpr int TERM_PAYLOAD = TERM_CELLS * 3; // 4770

/** CRC-CCITT (poly 0x1021). Seed defaults to 0xFFFF; pass prior CRC to continue. */
uint16_t crc16_ccitt(const uint8_t *data, size_t n, uint16_t seed = 0xFFFF);
