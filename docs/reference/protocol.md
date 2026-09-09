# Serial protocol

Authoritative constants: [`daemon/src/protocol.rs`](../../daemon/src/protocol.rs) and
[`firmware/src/protocol.h`](../../firmware/src/protocol.h) / [`protocol.cpp`](../../firmware/src/protocol.cpp). Keep both sides in sync.

## Link

USB-Serial/JTAG CDC to the ESP32-C3. Host writes framed messages and expects
ACK/NAK. Device may emit single-byte opcodes (optionally plus one data byte)
toward the host for OSD / mode.

## Host → device frame

| Field | Size | Notes |
|-------|------|-------|
| Magic | 4 | `AA 55 A5 5A` |
| Header | 4 | `seq`, `cx`, `cy`, `flags` |
| Payload | 4770 | `53 × 30 × 3` bytes |
| CRC | 2 | CRC-CCITT (init `0xFFFF`, poly `0x1021`) over header+payload |

Total frame length: **4780** bytes.

Cell encoding (terminal): three bytes per cell (glyph / colour pair as used
by the firmware terminal renderer). Status and style modes reuse the same
payload size; meaningful data occupies the leading StatusSnap region,
remainder zero.

### Flags (`flags` byte)

| Bit / value | Name | Meaning |
|-------------|------|---------|
| `0x01` | `FLAG_CURSOR_VISIBLE` | Cursor position is meaningful |
| `0x02` | `FLAG_CURSOR_ON` | Cursor blink phase on |
| `0x04` | `FLAG_ACTIVITY` | User activity — wake / refresh |
| `0x10` | `FLAG_STYLE` | Payload carries style (from StatusSnap packing) |
| `0x20` | `FLAG_STATUS` | Payload carries StatusSnap metrics |
| `0x80` | `FLAG_BYE` | Session teardown blank |

`cx` / `cy` are cursor coordinates when cursor flags are set (terminal).

### Device replies

| Byte | Name | Meaning |
|------|------|---------|
| `0x06` | ACK | Frame accepted |
| `0x15` | NAK | Reject / retry |

## Device → host opcodes

| Byte | Name | Payload |
|------|------|---------|
| `0x12` | `DEV_MODE_TOGGLE` | none — toggle status ↔ terminal |
| `0x13` | `DEV_OSD_BRIGHT` | 1 byte: `0` = auto, `1`–`6` = step |
| `0x14` | `DEV_OSD_SLEEP` | 1 byte: `0` = never, `1`–`6` = level |

The host maps bright/sleep into `buddy.config` `[display]` when connected.

## StatusSnap (v13)

- Version constant: `STATUS_VER = 13`
- Packed length: **4466** bytes (`STATUS_SNAP_LEN`)
- Carried in the frame payload when `FLAG_STATUS` (and style via
  `FLAG_STYLE` using the same packing helpers)

Includes (among other fields): hostname/date/time strings, CPU/MEM/DISK and
optional temperature, load/uptime/swap, up to **16** interfaces, up to
**80** service rows (40-byte names), alert mask/colours/hold, and
OSD-related style fields (brightness step, sleep level, auto day/night,
flags).

Service status codes and alert bit masks are defined in `protocol.rs`
(`ST_SVC_*`, `AL_*`, `OSD_F_*`). Secondary column ids: `SEC_UPTIME` /
`SEC_SWAP` / `SEC_LOAD` / `SEC_NONE`.

Native firmware tests (`pio test -e native`) and daemon unit tests assert
sizes and packing invariants — prefer those over re-deriving layouts by
hand.
