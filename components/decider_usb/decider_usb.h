#pragma once

#if defined(USE_ESP32_VARIANT_ESP32P4) || defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3)

#include <optional>
#include <string>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"

namespace esphome::decider_usb {

class DeciderUsb : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void set_boot_option(const std::string &choice_type, const std::optional<std::string> &entry_id = std::nullopt);
};

template<typename... Ts> class SetBootOptionAction : public Action<Ts...>, public Parented<DeciderUsb> {
 public:
  TEMPLATABLE_VALUE(std::string, choice_type)
  TEMPLATABLE_VALUE(std::string, entry_id)

  void play(const Ts &...x) override {
    std::optional<std::string> entry_id;
    if (this->entry_id_.has_value()) {
      entry_id = this->entry_id_.value(x...);
    }
    this->parent_->set_boot_option(this->choice_type_.value(x...), entry_id);
  }
};

}  // namespace esphome::decider_usb

#endif  // USE_ESP32_VARIANT_ESP32P4 || USE_ESP32_VARIANT_ESP32S2 || USE_ESP32_VARIANT_ESP32S3
