#pragma once

#include <cstdint>

#include "esphome/components/mqtt/mqtt_component.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome::mqtt {

class MQTTSensorComponent final : public MQTTComponent {
 public:
  explicit MQTTSensorComponent(sensor::Sensor *sensor) : sensor_(sensor) {}

  void set_expire_after(uint32_t expire_after) { this->expire_after = expire_after; }
  void disable_expire_after() { this->expire_after = 0; }
  uint32_t get_expire_after() const { return this->expire_after; }

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

  int setup_calls{0};
  int dump_calls{0};
  int discovery_calls{0};
  int initial_state_calls{0};
  uint32_t expire_after{0};

 protected:
  const char *component_type() const override { return "sensor"; }
  const EntityBase *get_entity() const override { return this->sensor_; }

  sensor::Sensor *sensor_;
};

}  // namespace esphome::mqtt
