#pragma once

#include <cstdint>

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include "../centurion_ble.h"

#ifdef USE_ESP32

namespace esphome::centurion_ble {

enum CenturionSensorType : uint8_t {
  CENTURION_SENSOR_BATTERY_VOLTAGE = 0,
  CENTURION_SENSOR_TIMER_REMAINING = 1,
  CENTURION_SENSOR_BEAM_RAW = 2,
};

class CenturionSensor : public sensor::Sensor, public Component {
 public:
  void set_hub(CenturionBLE *hub) { this->hub_ = hub; }
  void set_type(uint8_t type) { this->type_ = type; }

  void setup() override;
  void dump_config() override;

 protected:
  void publish_from_(const CenturionState &state);

  CenturionBLE *hub_{nullptr};
  uint8_t type_{CENTURION_SENSOR_BATTERY_VOLTAGE};
  bool published_{false};
};

}  // namespace esphome::centurion_ble

#endif  // USE_ESP32
