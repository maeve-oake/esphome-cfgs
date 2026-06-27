#include "rmt_carrier_dimmer.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <driver/gpio.h>
#include <cstring>
#include <cmath>
#endif

namespace esphome::rmt_carrier_dimmer {

static const char *const TAG = "rmt_carrier_dimmer";

void RMTCarrierDimmerOutput::setup() {
#ifdef USE_ESP32
#if SOC_RMT_SUPPORTED
  ESP_LOGCONFIG(TAG, "Setting up RMT carrier dimmer output...");

  if (!this->create_rmt_channel_()) {
    this->force_pin_low_fallback_();
    return;
  }
  if (!this->configure_carrier_()) {
    this->force_pin_low_fallback_();
    return;
  }
  this->stop_output_(true);
  return;
#endif
#endif
  ESP_LOGE(TAG, "RMT TX is not supported on this build target");
  this->mark_failed();
  this->force_pin_low_fallback_();
}

void RMTCarrierDimmerOutput::dump_config() {
  ESP_LOGCONFIG(TAG, "RMT Carrier Dimmer Output:");
  LOG_PIN("  Pin: ", this->pin_);
  ESP_LOGCONFIG(TAG, "  Carrier frequency: %" PRIu32 " Hz", this->carrier_frequency_hz_);
  ESP_LOGCONFIG(TAG, "  Carrier duty: %.1f%%", this->carrier_duty_ * 100.0f);
  ESP_LOGCONFIG(TAG, "  Envelope frequency: %.1f Hz", this->envelope_frequency_hz_);
  ESP_LOGCONFIG(TAG, "  Resolution: %" PRIu32 " Hz", this->RMT_RESOLUTION_HZ);
  LOG_FLOAT_OUTPUT(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "Component setup failed");
  }
}

void RMTCarrierDimmerOutput::write_state(float state) { this->update_output_(state); }

void RMTCarrierDimmerOutput::on_shutdown() { this->stop_output_(true); }

void RMTCarrierDimmerOutput::on_safe_shutdown() { this->stop_output_(true); }

void RMTCarrierDimmerOutput::on_powerdown() { this->stop_output_(true); }

bool RMTCarrierDimmerOutput::teardown() {
  this->stop_output_(true);
  return true;
}

uint32_t RMTCarrierDimmerOutput::envelope_period_ticks_() const {
  return static_cast<uint32_t>(std::lround(this->RMT_RESOLUTION_HZ / this->envelope_frequency_hz_));
}

void RMTCarrierDimmerOutput::update_output_(float state) {
#ifdef USE_ESP32
#if SOC_RMT_SUPPORTED
  if (this->is_failed() || this->channel_ == nullptr || this->encoder_ == nullptr) {
    return;
  }

  const float effective_duty = clamp(state, 0.0f, 1.0f);
  if (effective_duty <= 0.0f) {
    if (this->mode_ != OutputMode::OFF) {
      this->stop_output_(true);
    }
    return;
  }

  const uint32_t period_ticks = this->envelope_period_ticks_();
  uint32_t on_ticks = static_cast<uint32_t>(std::lround(period_ticks * effective_duty));
  on_ticks = std::min(on_ticks, period_ticks);

  if (on_ticks == 0) {
    if (this->mode_ != OutputMode::OFF) {
      this->stop_output_(true);
    }
    return;
  }

  if (on_ticks >= period_ticks) {
    if (this->mode_ == OutputMode::FULL) {
      return;
    }

    const uint32_t first_half = period_ticks / 2;
    const uint32_t second_half = period_ticks - first_half;
    const rmt_symbol_word_t symbol{
        .duration0 = static_cast<uint16_t>(first_half),
        .level0 = 1,
        .duration1 = static_cast<uint16_t>(second_half),
        .level1 = 1,
    };
    if (!this->start_loop_(symbol)) {
      this->stop_output_(true);
      return;
    }
    this->mode_ = OutputMode::FULL;
    this->last_on_ticks_ = period_ticks;
    this->last_off_ticks_ = 0;
    return;
  }

  const uint32_t off_ticks = period_ticks - on_ticks;
  if (this->mode_ == OutputMode::BURST && this->last_on_ticks_ == on_ticks && this->last_off_ticks_ == off_ticks) {
    return;
  }

  const rmt_symbol_word_t symbol{
      .duration0 = static_cast<uint16_t>(on_ticks),
      .level0 = 1,
      .duration1 = static_cast<uint16_t>(off_ticks),
      .level1 = 0,
  };
  if (!this->start_loop_(symbol)) {
    this->stop_output_(true);
    return;
  }
  this->mode_ = OutputMode::BURST;
  this->last_on_ticks_ = on_ticks;
  this->last_off_ticks_ = off_ticks;
#else
  (void) state;
#endif
#else
  (void) state;
#endif
}

bool RMTCarrierDimmerOutput::create_rmt_channel_() {
#ifdef USE_ESP32
#if SOC_RMT_SUPPORTED
  rmt_tx_channel_config_t config;
  memset(&config, 0, sizeof(config));
  config.gpio_num = static_cast<gpio_num_t>(this->pin_->get_pin());
  config.clk_src = RMT_CLK_SRC_DEFAULT;
  config.resolution_hz = this->RMT_RESOLUTION_HZ;
  config.mem_block_symbols = this->RMT_MEM_BLOCK_SYMBOLS;
  config.trans_queue_depth = this->RMT_TRANS_QUEUE_DEPTH;
  config.intr_priority = 0;
  config.flags.with_dma = false;
  config.flags.invert_out = this->pin_->is_inverted();
  config.flags.init_level = false;

  esp_err_t error = rmt_new_tx_channel(&config, &this->channel_);
  if (error != ESP_OK) {
    this->log_error_("rmt_new_tx_channel", error);
    this->mark_failed();
    return false;
  }

  rmt_copy_encoder_config_t encoder_config;
  memset(&encoder_config, 0, sizeof(encoder_config));
  error = rmt_new_copy_encoder(&encoder_config, &this->encoder_);
  if (error != ESP_OK) {
    this->log_error_("rmt_new_copy_encoder", error);
    this->mark_failed();
    return false;
  }
  return true;
#endif
#endif
  return false;
}

bool RMTCarrierDimmerOutput::configure_carrier_() {
#ifdef USE_ESP32
#if SOC_RMT_SUPPORTED
  rmt_carrier_config_t carrier;
  memset(&carrier, 0, sizeof(carrier));
  carrier.frequency_hz = this->carrier_frequency_hz_;
  carrier.duty_cycle = this->carrier_duty_;
  carrier.flags.polarity_active_low = false;
  carrier.flags.always_on = false;

  const esp_err_t error = rmt_apply_carrier(this->channel_, &carrier);
  if (error != ESP_OK) {
    this->log_error_("rmt_apply_carrier", error);
    this->mark_failed();
    return false;
  }
  return true;
#endif
#endif
  return false;
}

bool RMTCarrierDimmerOutput::enable_channel_() {
#ifdef USE_ESP32
#if SOC_RMT_SUPPORTED
  if (this->channel_enabled_) {
    return true;
  }
  const esp_err_t error = rmt_enable(this->channel_);
  if (error != ESP_OK) {
    this->log_error_("rmt_enable", error);
    this->status_set_warning();
    return false;
  }
  this->channel_enabled_ = true;
  return true;
#endif
#endif
  return false;
}

bool RMTCarrierDimmerOutput::disable_channel_() {
#ifdef USE_ESP32
#if SOC_RMT_SUPPORTED
  if (!this->channel_enabled_) {
    return true;
  }
  const esp_err_t error = rmt_disable(this->channel_);
  if (error != ESP_OK) {
    this->log_error_("rmt_disable", error);
    this->status_set_warning();
    return false;
  }
  this->channel_enabled_ = false;
  return true;
#endif
#endif
  return false;
}

bool RMTCarrierDimmerOutput::start_loop_(const rmt_symbol_word_t &symbol) {
#ifdef USE_ESP32
#if SOC_RMT_SUPPORTED
  this->stop_output_(false);
  if (!this->enable_channel_()) {
    return false;
  }

  rmt_transmit_config_t config;
  memset(&config, 0, sizeof(config));
  config.loop_count = -1;
  config.flags.eot_level = 0;

  const esp_err_t error = rmt_transmit(this->channel_, this->encoder_, &symbol, sizeof(symbol), &config);
  if (error != ESP_OK) {
    this->log_error_("rmt_transmit", error);
    this->status_set_warning();
    return false;
  }

  this->status_clear_warning();
  return true;
#endif
#endif
  return false;
}

bool RMTCarrierDimmerOutput::transmit_once_(const rmt_symbol_word_t &symbol) {
#ifdef USE_ESP32
#if SOC_RMT_SUPPORTED
  if (!this->enable_channel_()) {
    return false;
  }

  rmt_transmit_config_t config;
  memset(&config, 0, sizeof(config));
  config.loop_count = 0;
  config.flags.eot_level = 0;

  esp_err_t error = rmt_transmit(this->channel_, this->encoder_, &symbol, sizeof(symbol), &config);
  if (error != ESP_OK) {
    this->log_error_("rmt_transmit", error);
    this->status_set_warning();
    return false;
  }

  error = rmt_tx_wait_all_done(this->channel_, 1000);
  if (error != ESP_OK) {
    this->log_error_("rmt_tx_wait_all_done", error);
    this->status_set_warning();
    return false;
  }

  this->status_clear_warning();
  return true;
#endif
#endif
  return false;
}

void RMTCarrierDimmerOutput::stop_output_(bool force_idle_low) {
  this->mode_ = OutputMode::OFF;
  this->last_on_ticks_ = 0;
  this->last_off_ticks_ = 0;

#ifdef USE_ESP32
#if SOC_RMT_SUPPORTED
  if (this->channel_ != nullptr) {
    this->disable_channel_();
    if (force_idle_low && !this->ensure_idle_low_()) {
      this->force_pin_low_fallback_();
    }
    return;
  }
#endif
#endif

  if (force_idle_low) {
    this->force_pin_low_fallback_();
  }
}

bool RMTCarrierDimmerOutput::ensure_idle_low_() {
#ifdef USE_ESP32
#if SOC_RMT_SUPPORTED
  if (this->channel_ == nullptr || this->encoder_ == nullptr) {
    return false;
  }

  const rmt_symbol_word_t symbol{
      .duration0 = 1,
      .level0 = 0,
      .duration1 = 1,
      .level1 = 0,
  };

  const bool ok = this->transmit_once_(symbol);
  if (!this->disable_channel_()) {
    return false;
  }
  return ok;
#endif
#endif
  return false;
}

void RMTCarrierDimmerOutput::force_pin_low_fallback_() {
#ifdef USE_ESP32
  const auto gpio = static_cast<gpio_num_t>(this->pin_->get_pin());
  gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
  gpio_set_level(gpio, this->pin_->is_inverted() ? 1 : 0);
#endif
}

void RMTCarrierDimmerOutput::log_error_(const char *operation, esp_err_t error) {
  ESP_LOGE(TAG, "%s failed: %s", operation, esp_err_to_name(error));
}

}  // namespace esphome::rmt_carrier_dimmer
