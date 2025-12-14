#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"
#include "vauno.h"

namespace esphome {
namespace vauno {

static const char *const TAG = "vauno";

bool VaunoComponent::validate_(uint8_t *data) {
  uint8_t tail_bits = (data[5] >> 6) & 0x03;

  uint8_t sum = 0;
  for(uint8_t i = 0; i < 4; i++) {
    sum += (data[i] >> 4) + (data[i] & 0x0F);
  }
  sum += (data[4] >> 4);

  if(sum == 0) {
    return false;
  }

  uint8_t crc = ((data[4] & 0x0F) << 2) | tail_bits;
  return ((sum & 0x3F) == crc);
}

bool VaunoComponent::decode_sensor_(uint8_t *data) {
  uint8_t id = data[0];
  uint8_t channel   = ((data[1] & 0x30) >> 4) + 1;
  bool battery_low = (data[4] & 0x10) >> 4;
  int temp_raw = (int16_t)(((data[1] & 0x0f) << 12) | ((data[2] & 0xff) << 4));
  float temp_c  = (temp_raw >> 4) * 0.1f;
  int humidity  = (data[3] >> 1);

  if (channel < 1 || channel > 3) {
      return false;
  }

  if (humidity > 100) {
      return false;
  }

  auto *dev = this->find_device_(id, channel);

  if (dev) {
      if (dev->temperature) dev->temperature->publish_state(temp_c);
      if (dev->humidity) dev->humidity->publish_state(humidity);
      if (dev->battery_low) dev->battery_low->publish_state(battery_low);
      ESP_LOGD(TAG, "ID: %d (%02X), Channel: %d, Battery: %s, Temperature: %.1f°C, Humidity: %d%%", id, id, channel, battery_low ? "Low" : "OK", temp_c, humidity);
  } else {
      ESP_LOGW(TAG, "ID: %d (%02X), Channel: %d, Battery: %s, Temperature: %.1f°C, Humidity: %d%%", id, id, channel, battery_low ? "Low" : "OK", temp_c, humidity);
  }

  return true;
}

bool VaunoComponent::on_receive(remote_base::RemoteReceiveData data) {
  uint8_t bytes[6] = {0};
  uint32_t bits = 0;
  uint8_t sensor_id = 0;

  data.set_tolerance(400, remote_base::TOLERANCE_MODE_TIME);
  while (data.is_valid()) {
    if (data.expect_item(600, 9000)) {
        if (data.peek_item(600, 9000, 2)) {
            data.advance(2);
        }
        continue;
    }

    bool one = false;
    if (data.expect_item(600, 2000)) {
        one = false;
    } else if (data.expect_item(600, 4000)) {
        one = true;
    } else {
        data.advance();
        continue;
    }

    bytes[bits / 8] <<= 1;
    bytes[bits / 8] |= one ? 1 : 0;
    bits += 1;

    if (bits == 42) {
        bytes[bits / 8] <<= 6;
        bits = 0;
        if(!this->validate_(bytes)) {
            continue;
        }
        if (sensor_id == bytes[0]) {
            continue;
        }
        if (this->decode_sensor_(bytes)) {
            sensor_id = bytes[0];
        }
    }
  }
  return true;
}

}  // namespace vauno
}  // namespace esphome
