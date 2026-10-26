#pragma once

#include <cstdint>

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include "../centurion_ble.h"

#ifdef USE_ESP32

namespace esphome::centurion_ble {

enum CenturionTextSensorType : uint8_t {
  CENTURION_TEXT_DOOR_STATUS = 0,
  CENTURION_TEXT_POWER_STATUS = 1,
};

class CenturionTextSensor : public text_sensor::TextSensor, public Component {
 public:
  void set_hub(CenturionBLE *hub) { this->hub_ = hub; }
  void set_type(uint8_t type) { this->type_ = type; }

  void setup() override;
  void dump_config() override;

 protected:
  void publish_from_(const CenturionState &state);

  CenturionBLE *hub_{nullptr};
  uint8_t type_{CENTURION_TEXT_DOOR_STATUS};
  bool published_{false};
};

}  // namespace esphome::centurion_ble

#endif  // USE_ESP32
