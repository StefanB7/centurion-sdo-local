#pragma once

#include <string>

#include "esphome/components/mqtt/mqtt_component.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome::mqtt {

class MQTTTextSensor final : public MQTTComponent {
 public:
  explicit MQTTTextSensor(text_sensor::TextSensor *sensor) : sensor_(sensor) {}

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
  bool publish_state(const std::string &value) {
    (void) value;
    return true;
  }

  int setup_calls{0};
  int dump_calls{0};
  int discovery_calls{0};
  int initial_state_calls{0};

 protected:
  const char *component_type() const override { return "sensor"; }
  const EntityBase *get_entity() const override { return this->sensor_; }

  text_sensor::TextSensor *sensor_;
};

}  // namespace esphome::mqtt
