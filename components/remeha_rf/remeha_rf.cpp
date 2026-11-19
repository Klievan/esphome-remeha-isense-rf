#include "remeha_rf.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <SPI.h>
#ifdef REMEHA_RF_AXP2101
#include <Wire.h>
#include <XPowersLib.h>
#endif
#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif

namespace esphome {
namespace remeha_rf {

static const char *const TAG = "remeha_rf";

// SX1276 FSK registers
static const uint8_t REG_FIFO = 0x00, REG_OPMODE = 0x01, REG_BITRATE_MSB = 0x02, REG_FDEV_MSB = 0x04,
                     REG_FRF_MSB = 0x06, REG_PACONFIG = 0x09, REG_PARAMP = 0x0A, REG_LNA = 0x0C,
                     REG_RXCONFIG = 0x0D, REG_RXBW = 0x12, REG_OOKPEAK = 0x14, REG_PREAMBLEDETECT = 0x1F,
                     REG_PREAMBLE_MSB = 0x25, REG_SYNCCONFIG = 0x27, REG_SYNCVALUE1 = 0x28,
                     REG_PACKETCONFIG1 = 0x30, REG_PACKETCONFIG2 = 0x31, REG_PAYLOADLENGTH = 0x32,
                     REG_FIFOTHRESH = 0x35, REG_IRQFLAGS1 = 0x3E, REG_IRQFLAGS2 = 0x3F, REG_VERSION = 0x42,
                     REG_BITRATEFRAC = 0x5D;
static const uint8_t MODE_SLEEP = 0x00, MODE_STDBY = 0x01, MODE_TX = 0x03, MODE_RX = 0x05;
static const uint32_t DISPLAY_QUIET_MS = 120000;  // don't transmit while the real display was heard recently

// Poll cycle, like the display: status + control setpoint every other exchange, sensors in between.
static const uint8_t POLL[] = {0, 1, 25, 17, 0, 1, 28};

void RemehaSwitch::write_state(bool state) { parent_->on_switch(this, state); }
void RemehaNumber::control(float value) { parent_->on_number(this, value); }
void RemehaNumber::restore(float fallback) {
  pref_ = this->make_entity_preference<float>();
  float v;
  this->publish_state(pref_.load(&v) && !std::isnan(v) ? v : fallback);
}
void RemehaNumber::save(float value) { pref_.save(&value); }

uint8_t RemehaRF::rd_(uint8_t reg) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(cs_, LOW);
  SPI.transfer(reg & 0x7F);
  uint8_t v = SPI.transfer(0);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
  return v;
}

void RemehaRF::wr_(uint8_t reg, uint8_t v) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(cs_, LOW);
  SPI.transfer(reg | 0x80);
  SPI.transfer(v);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();
}

void RemehaRF::set_mode_(uint8_t mode) {  // FSK, HF band
  wr_(REG_OPMODE, mode);
  uint32_t t0 = millis();
  while (!(rd_(REG_IRQFLAGS1) & 0x80) && millis() - t0 < 20) {
  }
}

void RemehaRF::set_freq_() {
  uint32_t frf = ((uint64_t) (freq_mhz_ * 1e6) << 19) / 32000000ULL;
  wr_(REG_FRF_MSB, frf >> 16);
  wr_(REG_FRF_MSB + 1, frf >> 8);
  wr_(REG_FRF_MSB + 2, frf);
}

// Packet-mode RX: hardware preamble detect + kit-independent sync AA AA A3 (preamble tail + "0011"), fixed length.
void RemehaRF::configure_rx_() {
  set_mode_(MODE_STDBY);
  wr_(REG_LNA, 0x23);             // max gain, LNA boost HF
  wr_(REG_RXCONFIG, 0x08);        // AGC on, AFC off
  wr_(REG_RXBW, 0x09);            // 200 kHz (base sits ~11 kHz above the display)
  wr_(REG_OOKPEAK, 0x28);         // bit sync on
  wr_(REG_PREAMBLEDETECT, 0x8A);  // on, 1 byte (the sync word itself contains 20 more preamble chips), tol 10
  wr_(REG_SYNCCONFIG, 0x52);      // auto-restart RX, preamble 0xAA, sync on, 3 bytes
  for (int i = 0; i < remeha::SYNC_BYTES; i++) wr_(REG_SYNCVALUE1 + i, remeha::SYNC[i]);
  wr_(REG_PACKETCONFIG1, 0x00);   // fixed length, no DC-free, no CRC (ours is checked in software)
  wr_(REG_PACKETCONFIG2, 0x40);   // packet mode
  wr_(REG_PAYLOADLENGTH, remeha::RX_PAYLOAD_BYTES);
  set_mode_(MODE_RX);
}

bool RemehaRF::transmit_(uint8_t type, int ot_id, uint16_t value) {
  uint8_t buf[remeha::AIR_BYTES];
  int n = remeha::build(template_, type, ot_id, value, ++seq_, buf);
  set_mode_(MODE_STDBY);
  uint16_t fd = dev_khz_ * 1000 / 61.035f;
  wr_(REG_FDEV_MSB, fd >> 8);
  wr_(REG_FDEV_MSB + 1, fd);
  wr_(REG_PACONFIG, 0x80 | (tx_dbm_ - 2));  // PA_BOOST, Pout = 2 + OutputPower dBm (capped at 14 in config)
  wr_(REG_PARAMP, 0x09);
  wr_(REG_PREAMBLE_MSB, 0);
  wr_(REG_PREAMBLE_MSB + 1, 8);   // 8 x 0xAA, its last chip is the first 0 of the 000111 sync
  wr_(REG_SYNCCONFIG, 0x00);      // sync chips are part of our payload
  wr_(REG_PAYLOADLENGTH, n);
  wr_(REG_FIFOTHRESH, 0x8F);      // start TX when FIFO not empty
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(cs_, LOW);
  SPI.transfer(REG_FIFO | 0x80);
  for (int i = 0; i < n; i++) SPI.transfer(buf[i]);
  digitalWrite(cs_, HIGH);
  SPI.endTransaction();

  uint32_t t0 = micros();
  wr_(REG_OPMODE, MODE_TX);
  bool sent = false;
  while (micros() - t0 < 40000)  // ~16.5 ms on air
    if (rd_(REG_IRQFLAGS2) & 0x08) { sent = true; break; }
  last_tx_ms_ = millis();
  configure_rx_();  // reply arrives ~125 ms later
  ESP_LOGD(TAG, "TX %s id=%d val=0x%04X seq=%u %s", type == remeha::TYPE_WRITE ? "WRITE" : "READ", ot_id, value,
           seq_ & 7, sent ? "" : "(PacketSent timeout!)");
  return sent;
}

void RemehaRF::setup() {
#ifdef REMEHA_RF_AXP2101
  static XPowersAXP2101 pmu;
  if (pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, 21, 22)) {
    pmu.setALDO2Voltage(3300);
    pmu.enableALDO2();  // LoRa rail on T-Beam v1.2
  } else {
    ESP_LOGE(TAG, "AXP2101 not found, radio may be unpowered");
  }
  delay(20);
#endif
  pinMode(cs_, OUTPUT);
  digitalWrite(cs_, HIGH);
  pinMode(rst_, OUTPUT);
  digitalWrite(rst_, LOW);
  delay(2);
  digitalWrite(rst_, HIGH);
  delay(10);
  SPI.begin(sck_, miso_, mosi_, cs_);

  uint8_t ver = rd_(REG_VERSION);
  if (ver != 0x12) {
    ESP_LOGE(TAG, "SX127x not found (RegVersion=0x%02X)", ver);
    this->mark_failed();
    return;
  }
  set_mode_(MODE_SLEEP);  // LongRangeMode=0 (FSK) can only be set in sleep
  set_mode_(MODE_STDBY);
  set_freq_();
  wr_(REG_BITRATE_MSB, 976 >> 8);  // 32 MHz / (976 + 9/16) = 32768 chip/s
  wr_(REG_BITRATE_MSB + 1, 976 & 0xFF);
  wr_(REG_BITRATEFRAC, 9);
  configure_rx_();

  for (auto *s : {master_, ch_enable_, dhw_enable_}) {
    if (s == nullptr) continue;
    auto st = s->get_initial_state_with_restore_mode();
    s->publish_state(st.has_value() ? *st : false);
  }
  if (t_set_ != nullptr) t_set_->restore(40.0f);
  if (display_active_ != nullptr) display_active_->publish_state(false);
}

void RemehaRF::set_display_template(const std::string &hex) {
  if (!remeha::parse_template(hex, template_)) ESP_LOGE(TAG, "display_template is not 52 hex characters");
}

void RemehaRF::on_switch(RemehaSwitch *s, bool state) {
  s->publish_state(state);
  if (s == master_) ESP_LOGI(TAG, "RF master %s", state ? "ON: polling iBase" : "OFF: listen only");
  poll_idx_ = 0;  // send status/setpoint next
}

void RemehaRF::on_number(RemehaNumber *n, float value) {
  n->publish_state(value);
  n->save(value);
  poll_idx_ = 1;  // send control setpoint next
}

void RemehaRF::poll_() {
  if (!template_.valid) {
    if (!warned_no_template_) ESP_LOGE(TAG, "RF master needs display_template (see docs/pairing.md); not transmitting");
    warned_no_template_ = true;
    return;
  }
  uint8_t id = POLL[poll_idx_];
  poll_idx_ = (poll_idx_ + 1) % sizeof(POLL);
  if (id == 0) {
    uint16_t flags = (on_(ch_enable_) ? 0x01 : 0) | (on_(dhw_enable_) ? 0x02 : 0);
    transmit_(remeha::TYPE_READ, 0, flags << 8);  // ID 0 read carries the master enable flags
  } else if (id == 1) {
    float t = (t_set_ != nullptr && !std::isnan(t_set_->state)) ? t_set_->state : 10.0f;
    transmit_(remeha::TYPE_WRITE, 1, (uint16_t) lroundf(clamp(t, 0.0f, 90.0f) * 256));
  } else {
    transmit_(remeha::TYPE_READ, id, 0);
  }
}

void RemehaRF::handle_(const remeha::Frame &f) {
  if (template_.valid && !remeha::same_kit(f.bits, template_)) {
    ESP_LOGV(TAG, "Ignoring frame from another kit");
    rx_foreign_++;
    return;
  }
  (f.from_display ? rx_display_ : rx_base_)++;
  ESP_LOGD(TAG, "RX %s type=%02X id=%d val=0x%04X (%.2f)", f.from_display ? "display" : "base", f.type, f.ot_id,
           f.value, f.f88());
  if (f.from_display && !template_.valid && !logged_template_) {
    // Pairing aid: the display's own frame header is the TX template for this kit.
    ESP_LOGW(TAG, "Heard an iSense display. Put this in your config to enable RF master:");
    ESP_LOGW(TAG, "  display_template: \"%s\"", remeha::template_hex(f.bits).c_str());
    logged_template_ = true;
  }
  if (f.from_display) {
    // We never hear our own TX, so this is the real iSense display.
    if (last_display_ms_ == 0 || millis() - last_display_ms_ > DISPLAY_QUIET_MS)
      ESP_LOGW(TAG, "Real display is active%s", on_(master_) ? ": pausing our TX" : "");
    last_display_ms_ = millis();
    if (display_active_) display_active_->publish_state(true);
    return;
  }
  if (f.type != remeha::TYPE_REPLY) return;
  switch (f.ot_id) {
    case 0: {
      uint8_t s = f.value & 0xFF;  // slave status flags
      if (fault_) fault_->publish_state(s & 0x01);
      if (ch_active_) ch_active_->publish_state(s & 0x02);
      if (dhw_active_) dhw_active_->publish_state(s & 0x04);
      if (flame_) flame_->publish_state(s & 0x08);
      break;
    }
    case 17: if (rel_mod_) rel_mod_->publish_state(f.f88()); break;
    case 25: if (t_boiler_) t_boiler_->publish_state(f.f88()); break;
    case 28:  // observed: every other ID 28 reply carries 0x0000 (likely an invalid reading)
      if (t_ret_ && f.value != 0) t_ret_->publish_state(f.f88());
      break;
    default: break;
  }
}

void RemehaRF::loop() {
  uint32_t now = millis();
  if (rd_(REG_IRQFLAGS2) & 0x04) {  // PayloadReady
    uint8_t p[remeha::RX_PAYLOAD_BYTES];
    SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    digitalWrite(cs_, LOW);
    SPI.transfer(REG_FIFO);
    for (auto &b : p) b = SPI.transfer(0);
    digitalWrite(cs_, HIGH);
    SPI.endTransaction();
    wr_(REG_RXCONFIG, 0x08 | 0x40);  // RestartRxWithoutPllLock: re-arm preamble/sync detection
    auto f = remeha::decode(p);
    if (f.ok) {
      handle_(f);
    } else {
      rx_bad_++;
      ESP_LOGV(TAG, "RX frame failed CRC/Manchester");
    }
  }
  if (now - last_stats_ms_ > 60000) {
    last_stats_ms_ = now;
    ESP_LOGD(TAG, "RX last 60 s: base=%u display=%u bad=%u other-kit=%u", rx_base_, rx_display_, rx_bad_, rx_foreign_);
    rx_base_ = rx_display_ = rx_bad_ = rx_foreign_ = 0;
  }

  if (display_active_ && display_active_->state && now - last_display_ms_ > DISPLAY_QUIET_MS)
    display_active_->publish_state(false);

#ifdef USE_API
  // Fail-safe: HA gone for too long -> stop asking for heat.
  if (api::global_api_server != nullptr && api::global_api_server->is_connected()) {
    api_lost_ms_ = 0;
  } else if (api_lost_ms_ == 0) {
    api_lost_ms_ = now;
  } else if (failsafe_ms_ && now - api_lost_ms_ > failsafe_ms_ && on_(ch_enable_)) {
    ESP_LOGW(TAG, "No Home Assistant connection for %us: disabling CH", failsafe_ms_ / 1000);
    ch_enable_->publish_state(false);
  }
#endif

  if (!on_(master_) || now - last_tx_ms_ < poll_ms_) return;
  if (last_display_ms_ != 0 && now - last_display_ms_ < DISPLAY_QUIET_MS) return;
  poll_();
}

void RemehaRF::dump_config() {
  ESP_LOGCONFIG(TAG, "Remeha iBase RF master (SX1276)");
  ESP_LOGCONFIG(TAG, "  Frequency: %.4f MHz, TX %d dBm, deviation %.1f kHz", freq_mhz_, tx_dbm_, dev_khz_);
  ESP_LOGCONFIG(TAG, "  Poll interval: %u ms, failsafe: %u s", poll_ms_, failsafe_ms_ / 1000);
  ESP_LOGCONFIG(TAG, "  Display template: %s", template_.valid ? "configured" : "NOT SET (listen-only)");
  if (this->is_failed()) ESP_LOGE(TAG, "  Radio init FAILED");
}

}  // namespace remeha_rf
}  // namespace esphome
