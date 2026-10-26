#pragma once

#include <cstdint>

#include "esphome/components/mqtt/mqtt_component.h"
#include "esphome/components/cover/cover.h"

namespace esphome::mqtt {

// `final` mirrors ESPHome 2026.9.1: the wrapper must compose, not subclass.
class MQTTCoverComponent final : public MQTTComponent {
 public:
  explicit MQTTCoverComponent(cover::Cover *cover) : cover_(cover) {}

  void setup() override { this->setup_calls++; }
  void dump_config() override { this->dump_calls++; }
  void send_discovery(JsonObject root, SendDiscoveryConfig &config) override {
    this->discovery_calls++;
    (void) root;
    config.command_topic = true;
  }
  bool send_initial_state() override {
    this->initial_state_calls++;
    return true;
  }

  void set_custom_position_state_topic(const std::string &topic) { this->position_state_topic = topic; }
  void set_custom_position_command_topic(const std::string &topic) { this->position_command_topic = topic; }
  void set_custom_tilt_state_topic(const std::string &topic) { this->tilt_state_topic = topic; }
  void set_custom_tilt_command_topic(const std::string &topic) { this->tilt_command_topic = topic; }

  // Test observability.
  int setup_calls{0};
  int dump_calls{0};
  int discovery_calls{0};
  int initial_state_calls{0};
  std::string position_state_topic;
  std::string position_command_topic;
  std::string tilt_state_topic;
  std::string tilt_command_topic;

 protected:
  const char *component_type() const override { return "cover"; }
  const EntityBase *get_entity() const override { return this->cover_; }

  cover::Cover *cover_;
};

}  // namespace esphome::mqtt
