#pragma once

namespace esphome::esp32_ble_tracker {

class ESPBTDevice {};

class ESPBTDeviceListener {
 public:
  virtual ~ESPBTDeviceListener() = default;
  virtual bool parse_device(const ESPBTDevice & /*device*/) { return false; }

  void *parent_{nullptr};
};

}  // namespace esphome::esp32_ble_tracker
