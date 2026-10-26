#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "esphome/core/component.h"
#include "esphome/core/json_stub.h"
#include "esphome/components/mqtt/mqtt_client.h"

namespace esphome::mqtt {

struct SendDiscoveryConfig {
  bool state_topic{true};
  bool command_topic{true};
};

class MQTTComponent : public Component {
 public:
  void set_availability(std::string topic, std::string payload_available, std::string payload_not_available) {
    this->availability_topic_ = std::move(topic);
    this->availability_payload_available_ = std::move(payload_available);
    this->availability_payload_not_available_ = std::move(payload_not_available);
  }
  void disable_availability() { this->set_availability("", "", ""); }

  void set_qos(uint8_t qos) { this->qos_ = qos; }
  uint8_t get_qos() const { return this->qos_; }
  void set_retain(bool retain) { this->retain_ = retain; }
  bool get_retain() const { return this->retain_; }
  void set_subscribe_qos(uint8_t qos) { this->subscribe_qos_ = qos; }
  uint8_t get_subscribe_qos() const { return this->subscribe_qos_; }
  void set_command_retain(bool command_retain) { this->command_retain_ = command_retain; }
  bool get_command_retain() const { return this->command_retain_; }
  void disable_discovery() { this->discovery_enabled_ = false; }
  bool get_discovery_enabled() const { return this->discovery_enabled_; }

  template<typename T> void set_custom_state_topic(T &&custom_state_topic) {
    this->custom_state_topic_ = std::forward<T>(custom_state_topic);
  }
  template<typename T> void set_custom_command_topic(T &&custom_command_topic) {
    this->custom_command_topic_ = std::forward<T>(custom_command_topic);
  }
  const std::string &get_custom_state_topic() const { return this->custom_state_topic_; }
  const std::string &get_custom_command_topic() const { return this->custom_command_topic_; }

  virtual void send_discovery(JsonObject root, SendDiscoveryConfig &config) = 0;
  virtual bool send_initial_state() = 0;
  virtual const char *component_type() const = 0;

  // Test accessors mirroring the base fields send_discovery_() would emit.
  const std::string &single_availability_topic() const { return this->availability_topic_; }
  const std::string &single_payload_available() const { return this->availability_payload_available_; }
  const std::string &single_payload_not_available() const { return this->availability_payload_not_available_; }

 protected:
  virtual const EntityBase *get_entity() const = 0;

  std::string custom_state_topic_;
  std::string custom_command_topic_;
  std::string availability_topic_;
  std::string availability_payload_available_;
  std::string availability_payload_not_available_;
  uint8_t qos_{0};
  uint8_t subscribe_qos_{0};
  bool retain_{true};
  bool command_retain_{false};
  bool discovery_enabled_{true};
};

}  // namespace esphome::mqtt
