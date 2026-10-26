#pragma once

#include <cstdint>

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

#include "../centurion_ble.h"

#ifdef USE_ESP32

namespace esphome::centurion_ble {

enum CenturionBinarySensorType : uint8_t {
  CENTURION_BINARY_CONNECTED = 0,
  CENTURION_BINARY_MAINS_ON = 1,
  CENTURION_BINARY_BATTERY_LOW = 2,
  CENTURION_BINARY_COLLISION = 3,
  CENTURION_BINARY_MAX_COLLISIONS = 4,
  CENTURION_BINARY_STALLED = 5,
  CENTURION_BINARY_LOST = 6,
  CENTURION_BINARY_LIMITS_NOT_SET = 7,
  CENTURION_BINARY_XMR_ALARM = 8,
};

class CenturionBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void set_hub(CenturionBLE *hub) { this->hub_ = hub; }
  void set_type(uint8_t type) { this->type_ = type; }

  void setup() override;
  void dump_config() override;

 protected:
  void publish_from_(const CenturionState &state);

  CenturionBLE *hub_{nullptr};
  uint8_t type_{CENTURION_BINARY_CONNECTED};
};

}  // namespace esphome::centurion_ble

#endif  // USE_ESP32
