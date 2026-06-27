#pragma once

#include "esphome/components/output/float_output.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

#ifdef USE_ESP32
#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>
#include <soc/soc_caps.h>
#endif

namespace esphome::rmt_carrier_dimmer {

class RMTCarrierDimmerOutput : public output::FloatOutput, public Component {
 public:
  explicit RMTCarrierDimmerOutput(InternalGPIOPin *pin) : pin_(pin) {}

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  void write_state(float state) override;
  void on_shutdown() override;
  void on_safe_shutdown() override;
  void on_powerdown() override;
  bool teardown() override;

  void set_carrier_frequency(uint32_t carrier_frequency) { this->carrier_frequency_hz_ = carrier_frequency; }
  void set_carrier_duty(float carrier_duty) { this->carrier_duty_ = carrier_duty; }
  void set_envelope_frequency(float envelope_frequency) { this->envelope_frequency_hz_ = envelope_frequency; }

 protected:
  static constexpr uint32_t RMT_RESOLUTION_HZ = 20'000'000;
  static constexpr size_t RMT_MEM_BLOCK_SYMBOLS = 48;
  static constexpr size_t RMT_TRANS_QUEUE_DEPTH = 1;
  static constexpr uint32_t RMT_SYMBOL_DURATION_MAX = 0x7FFF;

  enum class OutputMode : uint8_t {
    OFF,
    BURST,
    FULL,
  };

  uint32_t envelope_period_ticks_() const;
  void update_output_(float state);
  bool create_rmt_channel_();
  bool configure_carrier_();
  bool enable_channel_();
  bool disable_channel_();
  bool start_loop_(const rmt_symbol_word_t &symbol);
  bool transmit_once_(const rmt_symbol_word_t &symbol);
  void stop_output_(bool force_idle_low);
  bool ensure_idle_low_();
  void force_pin_low_fallback_();
  void log_error_(const char *operation, esp_err_t error);

  InternalGPIOPin *pin_;

#ifdef USE_ESP32
#if SOC_RMT_SUPPORTED
  rmt_channel_handle_t channel_{nullptr};
  rmt_encoder_handle_t encoder_{nullptr};
#endif
#endif

  uint32_t carrier_frequency_hz_{205000};
  float carrier_duty_{0.50f};
  float envelope_frequency_hz_{1000.0f};
  OutputMode mode_{OutputMode::OFF};
  uint32_t last_on_ticks_{0};
  uint32_t last_off_ticks_{0};
  bool channel_enabled_{false};
};

}  // namespace esphome::rmt_carrier_dimmer
