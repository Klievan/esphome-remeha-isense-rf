#pragma once
// ESPHome component: OpenTherm master for a Remeha iBase RF over 868 MHz, replacing the iSense RF display.
// Radio: SX1276 (tested: LilyGO T-Beam v1.2). Listen-only until `master` is on and display_template is set.
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include "remeha_frame.h"

namespace esphome {
namespace remeha_rf {

class RemehaRF;

class RemehaSwitch : public switch_::Switch {
 public:
  void set_parent(RemehaRF *p) { parent_ = p; }

 protected:
  void write_state(bool state) override;
  RemehaRF *parent_{nullptr};
};

class RemehaNumber : public number::Number {
 public:
  void set_parent(RemehaRF *p) { parent_ = p; }
  void restore(float fallback);  // load last value from flash, else fallback
  void save(float value);

 protected:
  void control(float value) override;
  RemehaRF *parent_{nullptr};
  ESPPreferenceObject pref_;
};

class RemehaRF : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_pins(int sck, int miso, int mosi, int cs, int rst) { sck_ = sck; miso_ = miso; mosi_ = mosi; cs_ = cs; rst_ = rst; }
  void set_frequency(float mhz) { freq_mhz_ = mhz; }
  void set_tx_power(int dbm) { tx_dbm_ = dbm; }
  void set_deviation(float khz) { dev_khz_ = khz; }
  void set_poll_interval(uint32_t ms) { poll_ms_ = ms; }
  void set_failsafe_timeout(uint32_t ms) { failsafe_ms_ = ms; }
  void set_axp2101(bool v) { axp2101_ = v; }
  void set_display_template(const std::string &hex);

  void set_t_boiler_sensor(sensor::Sensor *s) { t_boiler_ = s; }
  void set_rel_mod_level_sensor(sensor::Sensor *s) { rel_mod_ = s; }
  void set_t_ret_sensor(sensor::Sensor *s) { t_ret_ = s; }
  void set_flame_on_binary_sensor(binary_sensor::BinarySensor *s) { flame_ = s; }
  void set_ch_active_binary_sensor(binary_sensor::BinarySensor *s) { ch_active_ = s; }
  void set_dhw_active_binary_sensor(binary_sensor::BinarySensor *s) { dhw_active_ = s; }
  void set_fault_indication_binary_sensor(binary_sensor::BinarySensor *s) { fault_ = s; }
  void set_display_active_binary_sensor(binary_sensor::BinarySensor *s) { display_active_ = s; }
  void set_master_switch(RemehaSwitch *s) { master_ = s; }
  void set_ch_enable_switch(RemehaSwitch *s) { ch_enable_ = s; }
  void set_dhw_enable_switch(RemehaSwitch *s) { dhw_enable_ = s; }
  void set_t_set_number(RemehaNumber *n) { t_set_ = n; }

  void on_switch(RemehaSwitch *s, bool state);
  void on_number(RemehaNumber *n, float value);

 protected:
  uint8_t rd_(uint8_t reg);
  void wr_(uint8_t reg, uint8_t v);
  void set_mode_(uint8_t mode);
  void set_freq_();
  void configure_rx_();
  bool transmit_(uint8_t type, int ot_id, uint16_t value);
  void poll_();
  void handle_(const remeha::Frame &f);
  bool on_(RemehaSwitch *s) const { return s != nullptr && s->state; }

  int sck_{5}, miso_{19}, mosi_{27}, cs_{18}, rst_{23};
  float freq_mhz_{868.2925f};
  int tx_dbm_{7};
  float dev_khz_{20.0f};
  uint32_t poll_ms_{4400};
  uint32_t failsafe_ms_{15 * 60 * 1000};
  bool axp2101_{true};

  sensor::Sensor *t_boiler_{nullptr}, *rel_mod_{nullptr}, *t_ret_{nullptr};
  binary_sensor::BinarySensor *flame_{nullptr}, *ch_active_{nullptr}, *dhw_active_{nullptr}, *fault_{nullptr},
      *display_active_{nullptr};
  RemehaSwitch *master_{nullptr}, *ch_enable_{nullptr}, *dhw_enable_{nullptr};
  RemehaNumber *t_set_{nullptr};

  uint32_t last_tx_ms_{0};
  uint32_t last_display_ms_{0};  // 0 = real display never heard
  uint32_t api_lost_ms_{0};
  uint8_t poll_idx_{0};
  uint8_t seq_{0};
  uint32_t last_stats_ms_{0}, rx_base_{0}, rx_display_{0}, rx_bad_{0}, rx_foreign_{0};
  remeha::Template template_;
  bool warned_no_template_{false}, logged_template_{false};
};

}  // namespace remeha_rf
}  // namespace esphome
