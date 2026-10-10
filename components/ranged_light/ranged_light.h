#pragma once

#include <vector>
#include "esphome/components/light/light_output.h"
#include "esphome/components/output/float_output.h"
#include "esphome/core/helpers.h"

namespace esphome::ranged_light {

// Limits use the original light slider's brightness scale, before gamma correction.
// Transitions interpolate physical power after range mapping and gamma correction.
// Connected hardware outputs must use their default 0..100% power range.
class RangedLightOutput : public light::LightOutput {
 public:
  void add_output(output::FloatOutput *output) { this->outputs_.push_back(output); }

  light::LightTraits get_traits() override {
    light::LightTraits traits;
    traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
    return traits;
  }

  void setup_state(light::LightState *state) override { this->state_ = state; }

  float get_min_brightness() const { return this->min_brightness_; }
  float get_max_brightness() const { return this->max_brightness_; }
  void set_min_brightness(float value) {
    this->min_brightness_ = clamp(value, 0.0f, this->max_brightness_);
  }
  void set_max_brightness(float value) {
    this->max_brightness_ = clamp(value, this->min_brightness_, 1.0f);
  }

  void write_state(light::LightState *state) override {
    this->write_power_(this->target_power_(state->current_values));
  }

  std::unique_ptr<light::LightTransformer> create_default_transition() override;

 protected:
  float target_power_(const light::LightColorValues &values) const {
    float brightness;
    values.as_brightness(&brightness);
    if (brightness <= 0.0f)
      return 0.0f;
    const float ranged = this->min_brightness_ + brightness * (this->max_brightness_ - this->min_brightness_);
    return this->state_->gamma_correct_lut(ranged);
  }

  void write_power_(float power) {
    this->current_power_ = power;
    for (auto *output : this->outputs_)
      output->set_level(power);
  }

  class Transition : public light::LightTransformer {
   public:
    explicit Transition(RangedLightOutput *parent) : parent_(parent) {}

    void start() override {
      // Capture actual output so an interrupted/reversed fade starts without a jump.
      this->start_power_ = this->parent_->current_power_;
      this->target_power_ = this->parent_->target_power_(this->target_values_);
    }

    optional<light::LightColorValues> apply() override {
      const float progress = smoothed_progress(this->get_progress_());
      this->parent_->write_power_(this->start_power_ + progress * (this->target_power_ - this->start_power_));
      // We wrote physical outputs directly; don't run the limits/gamma a second time.
      return {};
    }

   protected:
    RangedLightOutput *parent_;
    float start_power_{0.0f};
    float target_power_{0.0f};
  };

  light::LightState *state_{nullptr};
  std::vector<output::FloatOutput *> outputs_;
  float min_brightness_{0.0f};
  float max_brightness_{1.0f};
  float current_power_{0.0f};
};

inline std::unique_ptr<light::LightTransformer> RangedLightOutput::create_default_transition() {
  return make_unique<Transition>(this);
}

}  // namespace esphome::ranged_light
