// uart_link.h — inter-board UART framing (slice b2-13).
//
// The wire is Board 2's UART1 (P4 JST: IO17=TX, IO18=RX, GND) crossed to Board 1's
// GPIO17/18 — see docs/issues/board2/b2-13-uart-board1.md and esp32-appliance-design.md §6.
//
// Frame:  [SYNC 0x7E][TYPE u8][LEN u8][PAYLOAD ...LEN][CRC8]
//   - CRC8 (poly 0x07, init 0x00) covers TYPE, LEN and PAYLOAD (not SYNC).
//   - No byte-stuffing: a 0x7E inside the payload just makes that frame fail CRC; the
//     decoder then resyncs on the next SYNC. At our frame rates (~1 Hz heartbeat +
//     occasional events) the rare dropped frame is fine, and the next one recovers.
//   - TYPE is never 0x7E, so the decoder can treat a 0x7E while waiting for TYPE as a
//     fresh SYNC (resync), not a type byte.
//
// Pure C++ (no Arduino deps) on purpose: this exact file can be copied into the Board 1
// (WLED-MM) usermod so both ends share one definition of the protocol.

#pragma once

#include <stdint.h>
#include <stddef.h>

#define UART_SYNC          0x7E
#define UART_MAX_PAYLOAD   32     // PROVISION TLV (b2-10) is the largest; bump then if needed
#define UART_MAX_FRAME     (UART_MAX_PAYLOAD + 4)  // SYNC+TYPE+LEN+payload+CRC

enum UartFrameType : uint8_t {
  UART_KNOB_EVENT = 0x01,  // B1->B2 : knob u8 (0=mode,1=bright,2=gain), value i16 LE, flags u8 (bit0=press)
  UART_RECHECK    = 0x02,  // B2->B1 : empty — Board 1 fires an immediate /ingest
  UART_PROVISION  = 0x03,  // B2->B1 : TLV (b2-10, not built here)
  UART_RESET      = 0x04,  // either : empty (b2-10, not built here)
  UART_HEARTBEAT  = 0x05,  // both   : uptime_s u32 LE
};

static inline uint8_t uart_crc8(uint8_t crc, uint8_t b) {
  crc ^= b;
  for (int i = 0; i < 8; i++)
    crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  return crc;
}

// Encode a frame into `out` (must hold >= UART_MAX_FRAME). Returns total bytes, or 0 if
// the payload is too large / buffer too small.
static inline size_t uart_encode(uint8_t type, const uint8_t *payload, uint8_t len,
                                 uint8_t *out, size_t out_cap) {
  if (len > UART_MAX_PAYLOAD || (size_t)len + 4 > out_cap) return 0;
  uint8_t crc = uart_crc8(0, type);
  crc = uart_crc8(crc, len);
  size_t n = 0;
  out[n++] = UART_SYNC;
  out[n++] = type;
  out[n++] = len;
  for (uint8_t i = 0; i < len; i++) { out[n++] = payload[i]; crc = uart_crc8(crc, payload[i]); }
  out[n++] = crc;
  return n;
}

// Streaming decoder: feed received bytes one at a time. feed() returns true exactly when a
// CRC-valid frame completes, leaving `type`, `len`, `payload[]` set for that frame. Garbage
// (bad CRC, oversize LEN) silently drops the partial frame and resyncs on the next SYNC.
struct UartDecoder {
  uint8_t state = 0;   // 0=want SYNC, 1=want TYPE, 2=want LEN, 3=in PAYLOAD, 4=want CRC
  uint8_t type = 0, len = 0, idx = 0, crc = 0;
  uint8_t payload[UART_MAX_PAYLOAD];

  bool feed(uint8_t b) {
    switch (state) {
      case 0: if (b == UART_SYNC) state = 1; return false;
      case 1: // TYPE (a stray 0x7E here is a re-sync, not a type)
        if (b == UART_SYNC) return false;
        type = b; crc = uart_crc8(0, b); state = 2; return false;
      case 2: // LEN
        len = b;
        if (len > UART_MAX_PAYLOAD) { state = 0; return false; }
        crc = uart_crc8(crc, b); idx = 0; state = (len == 0) ? 4 : 3; return false;
      case 3: // PAYLOAD
        payload[idx++] = b; crc = uart_crc8(crc, b);
        if (idx >= len) state = 4; return false;
      case 4: { // CRC
        bool ok = (b == crc); state = 0; return ok;
      }
    }
    state = 0; return false;
  }
};

// --- typed payload helpers (little-endian) ----------------------------------
static inline size_t uart_make_recheck(uint8_t *out, size_t cap) {
  return uart_encode(UART_RECHECK, nullptr, 0, out, cap);
}
static inline size_t uart_make_heartbeat(uint32_t uptime_s, uint8_t *out, size_t cap) {
  uint8_t p[4] = { (uint8_t)uptime_s, (uint8_t)(uptime_s >> 8),
                   (uint8_t)(uptime_s >> 16), (uint8_t)(uptime_s >> 24) };
  return uart_encode(UART_HEARTBEAT, p, 4, out, cap);
}
static inline size_t uart_make_knob(uint8_t knob, int16_t value, uint8_t flags,
                                    uint8_t *out, size_t cap) {
  uint8_t p[4] = { knob, (uint8_t)value, (uint8_t)((uint16_t)value >> 8), flags };
  return uart_encode(UART_KNOB_EVENT, p, 4, out, cap);
}
static inline bool uart_parse_knob(const UartDecoder &d, uint8_t *knob, int16_t *value, uint8_t *flags) {
  if (d.type != UART_KNOB_EVENT || d.len != 4) return false;
  *knob = d.payload[0];
  *value = (int16_t)((uint16_t)d.payload[1] | ((uint16_t)d.payload[2] << 8));
  *flags = d.payload[3];
  return true;
}
static inline bool uart_parse_heartbeat(const UartDecoder &d, uint32_t *uptime_s) {
  if (d.type != UART_HEARTBEAT || d.len != 4) return false;
  *uptime_s = (uint32_t)d.payload[0] | ((uint32_t)d.payload[1] << 8) |
              ((uint32_t)d.payload[2] << 16) | ((uint32_t)d.payload[3] << 24);
  return true;
}
