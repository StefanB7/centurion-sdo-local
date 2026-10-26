#pragma once

#include "esphome/components/mqtt/mqtt_component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome::mqtt {

class MQTTBinarySensorComponent final : public MQTTComponent {
 public:
  explicit MQTTBinarySensorComponent(binary_sensor::BinarySensor *binary_sensor) : binary_sensor_(binary_sensor) {}

  void setup() override { this->setup_calls++; }
  void dump_config() override { this->dump_calls++; }
  void send_discovery(JsonObject root, SendDiscoveryConfig &config) override {
    this->discovery_calls++;
    (void) root;
    config.command_topic = false;
  }
  bool send_initial_state() override {
    this->initial_state_calls++;
    return true;
  }
  bool publish_state(bool state) {
    (void) state;
    return true;
  }

  int setup_calls{0};
  int dump_calls{0};
  int discovery_calls{0};
  int initial_state_calls{0};

 protected:
  const char *component_type() const override { return "binary_sensor"; }
  const EntityBase *get_entity() const override { return this->binary_sensor_; }

  binary_sensor::BinarySensor *binary_sensor_;
};

}  // namespace esphome::mqtt
