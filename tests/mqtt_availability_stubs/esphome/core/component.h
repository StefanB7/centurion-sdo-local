#pragma once

namespace esphome {

class EntityBase {};

class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void dump_config() {}
  virtual void call_setup() {}
};

}  // namespace esphome
