#pragma once

#include "esphome/core/component.h"

namespace esphome::sensor {

class Sensor : public EntityBase {
 public:
  float state{0.0f};
};

}  // namespace esphome::sensor
