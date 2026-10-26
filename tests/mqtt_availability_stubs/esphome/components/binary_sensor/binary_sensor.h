#pragma once

#include "esphome/core/component.h"

namespace esphome::binary_sensor {

class BinarySensor : public EntityBase {
 public:
  bool state{false};
};

}  // namespace esphome::binary_sensor
