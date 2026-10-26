#pragma once

namespace esphome {

// Minimal stand-in for the ESPHome component base. The entity classes only
// need the virtual lifecycle hooks they override.
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
};

}  // namespace esphome
