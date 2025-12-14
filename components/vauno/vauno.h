#pragma once

#include "esphome/components/remote_receiver/remote_receiver.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace vauno {

struct VaunoDevice {
    uint8_t id;
    uint8_t channel;

    sensor::Sensor *temperature{nullptr};
    sensor::Sensor *humidity{nullptr};
    binary_sensor::BinarySensor *battery_low{nullptr};
};

class VaunoComponent : public Component, public remote_base::RemoteReceiverListener {
 public:
  void add_device(uint8_t id, uint8_t channel,
                  sensor::Sensor *temperature = nullptr,
                  sensor::Sensor *humidity = nullptr,
                  binary_sensor::BinarySensor *battery_low = nullptr) {
    devices_.push_back(VaunoDevice{id, channel, temperature, humidity, battery_low});
  }
  bool on_receive(remote_base::RemoteReceiveData data) override;

 protected:
  bool decode_sensor_(uint8_t *data);
  bool validate_(uint8_t *data);

  std::vector<VaunoDevice> devices_;
  VaunoDevice *find_device_(uint8_t id, uint8_t channel) {
    for (auto &d : devices_)
      if (d.id == id && d.channel == channel) return &d;
    return nullptr;
  }
};

}  // namespace vauno
}  // namespace esphome
