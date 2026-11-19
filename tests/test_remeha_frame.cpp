// Unit tests for components/remeha_rf/remeha_frame.h (host build, no ESPHome needed):
//   c++ -std=c++17 -Wall -Wextra -o /tmp/test_remeha_frame tests/test_remeha_frame.cpp && /tmp/test_remeha_frame
//
// All vectors use a SYNTHETIC kit (random kit-ID bits). Expected values were produced by the separate Python
// reference decoder used during reverse engineering, which was validated against 217 real captured frames.
#include "../components/remeha_rf/remeha_frame.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
      failures++;                                                      \
    }                                                                  \
  } while (0)

static const char *TEMPLATE = "9D2E09896916C76AABFF9CF1270AE436494B87C6009000040000";
// Display WRITE id=1 (control setpoint) 55.0 C, seq 3: expected on-air chips (after the radio preamble).
static const char *AIR_W1_55 =
    "3B4ACD32B5534B534CB34D4CB2D4ACB333332AAAAB4AD2AD4D34AD5332B4D52CB4D34CD32B4AAAD4B54AD34CD2ACD554D2CAD5552AD2"
    "ACCCD555";
// iBase REPLY id=0 value 0x030A (CH+DHW enabled / CH active + flame), seq 4: RX payload after AA AA A3.
static const char *RX_BASE_R0 =
    "B4ACD32B5534B534CB34D4CB2D4ACB333332AAAAB4AD2AD4D34AD5332B4D52B34D34CD4AB34AAD4B534D34CD2AD55555552D5332D4CB"
    "3555";

static std::string hex(const uint8_t *b, int n) {
  std::string s;
  char t[3];
  for (int i = 0; i < n; i++) { snprintf(t, sizeof t, "%02X", b[i]); s += t; }
  return s;
}
static void unhex(const char *h, uint8_t *o, int n) {
  for (int i = 0; i < n; i++) { char t[3] = {h[2 * i], h[2 * i + 1], 0}; o[i] = (uint8_t) strtol(t, nullptr, 16); }
}

// Our own TX chips -> what a receiver syncing on AA AA A3 hands to decode(): drop the leading "0011".
static void air_to_rx_payload(const uint8_t *air, uint8_t *payload) {
  memset(payload, 0, remeha::RX_PAYLOAD_BYTES);
  for (int i = 0; i < remeha::RX_PAYLOAD_BYTES * 8; i++) {
    int src = i + 4;
    int bit = src < remeha::AIR_BYTES * 8 ? (air[src >> 3] >> (7 - (src & 7))) & 1 : 0;
    if (bit) payload[i >> 3] |= 0x80 >> (i & 7);
  }
}

int main() {
  remeha::Template t;
  CHECK(!remeha::parse_template("1234", t));
  CHECK(!remeha::parse_template(std::string(51, 'A') + "G", t));
  CHECK(remeha::parse_template(TEMPLATE, t) && t.valid);

  // Known-answer build (pins bit layout, Manchester polarity, CRC).
  uint8_t air[remeha::AIR_BYTES];
  CHECK(remeha::build(t, remeha::TYPE_WRITE, 1, 0x3700, 3, air) == remeha::AIR_BYTES);
  CHECK(hex(air, remeha::AIR_BYTES).rfind(AIR_W1_55, 0) == 0);

  // Round trip through the RX path.
  uint8_t p[remeha::RX_PAYLOAD_BYTES];
  air_to_rx_payload(air, p);
  remeha::Frame f = remeha::decode(p);
  CHECK(f.ok);
  CHECK(f.type == remeha::TYPE_WRITE && f.ot_id == 1 && f.value == 0x3700 && f.seq == 3 && f.from_display);
  CHECK(f.f88() == 55.0f);
  CHECK(remeha::same_kit(f.bits, t));
  CHECK(remeha::template_hex(f.bits) == TEMPLATE);  // learning the template from a display frame

  // Known-answer decode of a base reply.
  unhex(RX_BASE_R0, p, remeha::RX_PAYLOAD_BYTES);
  f = remeha::decode(p);
  CHECK(f.ok && !f.from_display && f.type == remeha::TYPE_REPLY && f.ot_id == 0 && f.value == 0x030A);
  CHECK(remeha::same_kit(f.bits, t));

  // Corruption is rejected: one flipped Manchester pair (valid chips, wrong bit) -> CRC fail.
  uint8_t bad[remeha::RX_PAYLOAD_BYTES];
  memcpy(bad, p, sizeof bad);
  bad[10] ^= 0x60;  // chips 81,82 = one Manchester pair (pairs start at odd chip indices): 01 <-> 10
  remeha::Frame g = remeha::decode(bad);
  CHECK(!g.ok && g.bits[40] != f.bits[40]);  // decoded fine, bit 40 flipped, CRC catches it
  // Manchester violation (00 pair) -> rejected before CRC.
  memcpy(bad, p, sizeof bad);
  bad[10] &= ~0x60;  // pair 81,82 -> 00
  CHECK(!remeha::decode(bad).ok);
  // A frame from another kit decodes but does not match.
  remeha::Template other = t;
  other.bits[5] ^= 1;
  CHECK(!remeha::same_kit(f.bits, other));

  // Negative f8.8.
  remeha::build(t, remeha::TYPE_WRITE, 16, (uint16_t) -384, 0, air);  // -1.5 C
  air_to_rx_payload(air, p);
  f = remeha::decode(p);
  CHECK(f.ok && f.f88() == -1.5f);

  if (failures == 0) printf("all remeha_frame tests passed\n");
  return failures ? 1 : 0;
}
