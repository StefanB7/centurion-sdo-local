#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace esphome::esp32_ble_tracker {

enum class ClientState : uint8_t {
  INIT = 0,
  DISCONNECTED,
  CONNECTING,
  CONNECTED,
  ESTABLISHED,
};

class ESPBTUUID {
 public:
  static ESPBTUUID from_raw(const char *raw) {
    ESPBTUUID uuid;
    uuid.raw_ = raw;
    return uuid;
  }

  const std::string &raw() const { return this->raw_; }

 protected:
  std::string raw_;
};

struct ESPBTManufacturerData {
  std::vector<uint8_t> data;
};

class ESPBTDevice {
 public:
  uint64_t address_uint64() const { return this->address_; }
  const std::vector<ESPBTManufacturerData> &get_manufacturer_datas() const {
    return this->manufacturer_datas_;
  }

  void set_address(uint64_t address) { this->address_ = address; }
  std::vector<ESPBTManufacturerData> manufacturer_datas_;

 protected:
  uint64_t address_{0};
};

// Both real ESPHome mixins declare a protected `parent_`, which is why the
// component has to qualify the BLE client one explicitly.
class ESPBTDeviceListener {
 public:
  virtual ~ESPBTDeviceListener() = default;
  virtual bool parse_device(const ESPBTDevice &device) = 0;

 protected:
  void *parent_{nullptr};
};

}  // namespace esphome::esp32_ble_tracker
