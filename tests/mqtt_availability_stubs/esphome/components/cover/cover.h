#pragma once

#include "esphome/core/component.h"

namespace esphome::cover {

class Cover : public EntityBase {
 public:
  float position{0.5f};
};

}  // namespace esphome::cover
