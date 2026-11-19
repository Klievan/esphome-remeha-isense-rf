#pragma once
// Remeha iSense RF <-> iBase RF frame codec. No ESPHome/Arduino dependencies (host-testable, see tests/).
// Field map and evidence levels: docs/protocol.md
//
// On air: 0xAA... preamble | "00111" | Manchester(206 data bits + CRC-16), chip pairs 01 = 1, 10 = 0,
// 32768 chip/s. The radio syncs on AA AA A3 (preamble tail + "0011"); the payload therefore starts with
// the final "1" of "00111" followed by the Manchester chips.
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

namespace remeha {

constexpr int DATA_BITS = 206;
constexpr int FRAME_BITS = DATA_BITS + 16;
constexpr int KIT_ID_BITS = 124;  // bits 0..123: constant per paired kit (display + iBase)
constexpr uint8_t TYPE_WRITE = 0x1C, TYPE_READ = 0x20, TYPE_REPLY = 0x24;

constexpr uint8_t SYNC[3] = {0xAA, 0xAA, 0xA3};
constexpr int SYNC_BYTES = 3;
constexpr int RX_PAYLOAD_BYTES = 56;  // 1 + 2 * FRAME_BITS = 445 chips, rounded up
constexpr int AIR_BYTES = 58;         // TX payload after the radio's preamble, byte-padded
constexpr int TEMPLATE_HEX_CHARS = 52;  // 206 bits + 2 zero bits

// A display frame (bits 0..205) with seq/type/id/value zeroed. Kit-specific: learned from the real
// display (the component logs it), configured in YAML as `display_template`.
struct Template {
  uint8_t bits[DATA_BITS];
  bool valid{false};
};

struct Frame {
  bool ok;  // Manchester-clean and CRC valid
  uint8_t type;
  int ot_id;
  uint8_t seq;
  bool from_display;
  uint16_t value;
  uint8_t bits[FRAME_BITS];
  float f88() const { return int16_t(value) / 256.0f; }
};

inline uint16_t crc16(const uint8_t *bits, int n) {  // poly 0x3D65 (EN 13757), MSB-first, init 0x1A12
  uint16_t r = 0x1A12;
  for (int i = 0; i < n; i++) {
    bool fb = ((r >> 15) & 1) ^ bits[i];
    r <<= 1;
    if (fb) r ^= 0x3D65;
  }
  return r;
}

inline void put(uint8_t *bits, int at, int width, uint32_t v) {
  for (int i = 0; i < width; i++) bits[at + i] = (v >> (width - 1 - i)) & 1;
}
inline uint32_t get(const uint8_t *bits, int at, int width) {
  uint32_t v = 0;
  for (int i = 0; i < width; i++) v = (v << 1) | bits[at + i];
  return v;
}

// Fields that differ between frames of one kit; zeroed in a template.
inline void clear_variable_fields(uint8_t *bits) {
  put(bits, 146, 3, 0);    // seq
  put(bits, 160, 8, 0);    // type
  put(bits, 172, 10, 0);   // OpenTherm data-id + 0x13C
  put(bits, 190, 16, 0);   // value
}

inline bool parse_template(const std::string &hex, Template &t) {
  t.valid = false;
  if (hex.size() != TEMPLATE_HEX_CHARS) return false;
  for (int i = 0; i < DATA_BITS; i++) {
    char c = hex[i / 4];
    int nib = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    if (nib < 0) return false;
    t.bits[i] = (nib >> (3 - i % 4)) & 1;
  }
  clear_variable_fields(t.bits);
  t.valid = true;
  return true;
}

inline std::string template_hex(const uint8_t *frame_bits) {
  uint8_t b[DATA_BITS + 2] = {0};
  memcpy(b, frame_bits, DATA_BITS);
  clear_variable_fields(b);
  static const char *H = "0123456789ABCDEF";
  std::string s;
  for (int i = 0; i < DATA_BITS + 2; i += 4) s += H[get(b, i, 4)];
  return s;
}

inline bool same_kit(const uint8_t *frame_bits, const Template &t) {
  return t.valid && memcmp(frame_bits, t.bits, KIT_ID_BITS) == 0;
}

// Build a display->base frame as on-air chips (radio FIFO bytes, sent after an 0xAA preamble).
inline int build(const Template &t, uint8_t type, int ot_id, uint16_t value, uint8_t seq, uint8_t *out) {
  uint8_t bits[FRAME_BITS];
  memcpy(bits, t.bits, DATA_BITS);
  put(bits, 146, 3, seq & 7);
  put(bits, 160, 8, type);
  put(bits, 172, 10, ot_id + 0x13C);
  put(bits, 190, 16, value);
  put(bits, DATA_BITS, 16, crc16(bits, DATA_BITS));

  memset(out, 0, AIR_BYTES);
  int c = 0;
  auto chip = [&](int b) {
    if (b) out[c >> 3] |= 0x80 >> (c & 7);
    c++;
  };
  for (int b : {0, 0, 1, 1, 1}) chip(b);  // the preamble's last chip is the first 0 of the on-air 000111
  for (int i = 0; i < FRAME_BITS; i++) { chip(!bits[i]); chip(bits[i]); }  // 1 -> 01, 0 -> 10
  while (c < AIR_BYTES * 8) { chip(1); chip(0); }
  return AIR_BYTES;
}

// Decode RX_PAYLOAD_BYTES of chips that followed the AA AA A3 sync.
inline Frame decode(const uint8_t *payload) {
  Frame f{};
  auto chip = [&](int i) { return (payload[i >> 3] >> (7 - (i & 7))) & 1; };
  if (!chip(0)) return f;  // must be the final 1 of "00111"
  for (int i = 0; i < FRAME_BITS; i++) {
    int a = chip(1 + 2 * i), b = chip(2 + 2 * i);
    if (a == b) return f;  // Manchester violation: truncated or corrupt
    f.bits[i] = b;         // 01 -> 1, 10 -> 0
  }
  f.ok = crc16(f.bits, DATA_BITS) == get(f.bits, DATA_BITS, 16);
  f.type = get(f.bits, 160, 8);
  f.ot_id = int(get(f.bits, 172, 10)) - 0x13C;
  f.seq = get(f.bits, 146, 3);
  f.from_display = get(f.bits, 182, 8) == 1;
  f.value = get(f.bits, 190, 16);
  return f;
}

}  // namespace remeha
