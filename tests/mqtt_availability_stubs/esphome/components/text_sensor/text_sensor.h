#pragma once

#include <string>

#include "esphome/core/component.h"

namespace esphome::text_sensor {

class TextSensor : public EntityBase {
 public:
  std::string state;
};

}  // namespace esphome::text_sensor
