#pragma once

// Combined MQTT availability for Centurion operator entities.
//
// Stock ESPHome emits a single `availability_topic` per entity and treats a
// configured per-entity `availability:` as a replacement for the node birth /
// last-will topic. Operator entities need both: the node must be online *and*
// the BLE session must be usable. Home Assistant expresses that as an
// `availability` list plus `availability_mode: all`.
//
// The built-in MQTT entity classes (MQTTCoverComponent, MQTTSensorComponent,
// MQTTBinarySensorComponent, MQTTTextSensor) are `final`, so they cannot be
// subclassed. These wrappers derive from mqtt::MQTTComponent instead and hold
// the built-in component by value: the wrapped component keeps owning state
// publishing and command subscriptions, while the wrapper owns registration and
// writes the combined discovery payload.
//
// Only used when an operator entity has both an MQTT id and a non-empty
// `availability:` override; every other entity keeps stock ESPHome behaviour.

#include "esphome/core/defines.h"

#ifdef USE_MQTT

#include <cstdint>
#include <string>

#include "esphome/components/mqtt/mqtt_client.h"
#include "esphome/components/mqtt/mqtt_component.h"

#ifdef USE_COVER
#include "esphome/components/cover/cover.h"
#include "esphome/components/mqtt/mqtt_cover.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/mqtt/mqtt_sensor.h"
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/mqtt/mqtt_binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/mqtt/mqtt_text_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome::centurion_ble {

template<typename InnerT, typename EntityT> class MqttAvailabilityComponent : public mqtt::MQTTComponent {
 public:
  explicit MqttAvailabilityComponent(EntityT *entity, const char *component_type)
      : inner_(entity), entity_(entity), component_type_(component_type) {}

  MqttAvailabilityComponent(const MqttAvailabilityComponent &) = delete;
  MqttAvailabilityComponent &operator=(const MqttAvailabilityComponent &) = delete;

  // Codegen calls this instead of mqtt::MQTTComponent::set_availability().
  // Keep the operator topic out of the base class' single-availability slot so
  // send_discovery_() does not also emit the singular availability_topic keys;
  // the operator entry is merged into the availability list below.
  void set_availability(std::string topic, std::string payload_available, std::string payload_not_available) {
    this->operator_topic_ = std::move(topic);
    this->operator_payload_available_ = std::move(payload_available);
    this->operator_payload_not_available_ = std::move(payload_not_available);
    mqtt::MQTTComponent::disable_availability();
  }
  void disable_availability() {
    this->operator_topic_.clear();
    this->operator_payload_available_.clear();
    this->operator_payload_not_available_.clear();
    mqtt::MQTTComponent::disable_availability();
  }

  // Mirror codegen-time settings onto the wrapped component: it is the object
  // that publishes state and subscribes to command topics.
  void set_qos(uint8_t qos) {
    mqtt::MQTTComponent::set_qos(qos);
    this->inner_.set_qos(qos);
  }
  void set_retain(bool retain) {
    mqtt::MQTTComponent::set_retain(retain);
    this->inner_.set_retain(retain);
  }
  void set_subscribe_qos(uint8_t qos) {
    mqtt::MQTTComponent::set_subscribe_qos(qos);
    this->inner_.set_subscribe_qos(qos);
  }
  void set_command_retain(bool command_retain) {
    mqtt::MQTTComponent::set_command_retain(command_retain);
    this->inner_.set_command_retain(command_retain);
  }
  void disable_discovery() {
    mqtt::MQTTComponent::disable_discovery();
    this->inner_.disable_discovery();
  }
  template<typename T> void set_custom_state_topic(T topic) {
    mqtt::MQTTComponent::set_custom_state_topic(topic);
    this->inner_.set_custom_state_topic(topic);
  }
  template<typename T> void set_custom_command_topic(T topic) {
    mqtt::MQTTComponent::set_custom_command_topic(topic);
    this->inner_.set_custom_command_topic(topic);
  }

  void setup() override { this->inner_.setup(); }
  void dump_config() override { this->inner_.dump_config(); }
  bool send_initial_state() override { return this->inner_.send_initial_state(); }

  void send_discovery(JsonObject root, mqtt::SendDiscoveryConfig &config) override {
    // Let the stock component add its entity-specific fields (and adjust
    // config.state_topic / config.command_topic).
    this->inner_.send_discovery(root, config);

    // The door has no stop control here; a null payload_stop removes Home
    // Assistant's default STOP button from the cover.
    if (std::string(this->component_type_) == "cover")
      root["payload_stop"] = nullptr;

    const mqtt::Availability &node = mqtt::global_mqtt_client->get_availability();
    const bool has_node = !node.topic.empty();
    const bool has_operator = !this->operator_topic_.empty();
    if (!has_node && !has_operator)
      return;

    JsonArray availability = root["availability"].to<JsonArray>();
    if (has_node) {
      this->add_availability_(availability, node.topic, node.payload_available, node.payload_not_available);
    }
    if (has_operator) {
      this->add_availability_(availability, this->operator_topic_, this->operator_payload_available_,
                              this->operator_payload_not_available_);
    }
    // Full key names on purpose: this stays unambiguous regardless of ESPHome's
    // MQTT key abbreviation setting.
    root["availability_mode"] = "all";
  }

 protected:
  const char *component_type() const override { return this->component_type_; }
  const EntityBase *get_entity() const override { return this->entity_; }

  static void add_availability_(JsonArray &availability, const std::string &topic,
                                const std::string &payload_available, const std::string &payload_not_available) {
    JsonObject entry = availability.add<JsonObject>();
    entry["topic"] = topic.c_str();
    entry["payload_available"] = payload_available.empty() ? "online" : payload_available.c_str();
    entry["payload_not_available"] = payload_not_available.empty() ? "offline" : payload_not_available.c_str();
  }

  InnerT inner_;
  EntityT *entity_;
  const char *component_type_;
  std::string operator_topic_;
  std::string operator_payload_available_;
  std::string operator_payload_not_available_;
};

#ifdef USE_COVER
class CenturionMqttCover : public MqttAvailabilityComponent<mqtt::MQTTCoverComponent, cover::Cover> {
  using Base = MqttAvailabilityComponent<mqtt::MQTTCoverComponent, cover::Cover>;

 public:
  explicit CenturionMqttCover(cover::Cover *entity) : Base(entity, "cover") {}

  void set_custom_position_state_topic(const std::string &topic) {
    this->inner_.set_custom_position_state_topic(topic);
  }
  void set_custom_position_command_topic(const std::string &topic) {
    this->inner_.set_custom_position_command_topic(topic);
  }
  void set_custom_tilt_state_topic(const std::string &topic) {
    this->inner_.set_custom_tilt_state_topic(topic);
  }
  void set_custom_tilt_command_topic(const std::string &topic) {
    this->inner_.set_custom_tilt_command_topic(topic);
  }
#ifdef USE_MQTT_COVER_JSON
  void set_use_json_format(bool use_json_format) { this->inner_.set_use_json_format(use_json_format); }
#endif
};
#endif  // USE_COVER

#ifdef USE_SENSOR
class CenturionMqttSensor : public MqttAvailabilityComponent<mqtt::MQTTSensorComponent, sensor::Sensor> {
  using Base = MqttAvailabilityComponent<mqtt::MQTTSensorComponent, sensor::Sensor>;

 public:
  explicit CenturionMqttSensor(sensor::Sensor *entity) : Base(entity, "sensor") {}

  void set_expire_after(uint32_t expire_after) { this->inner_.set_expire_after(expire_after); }
  void disable_expire_after() { this->inner_.disable_expire_after(); }
};
#endif  // USE_SENSOR

#ifdef USE_BINARY_SENSOR
class CenturionMqttBinarySensor
    : public MqttAvailabilityComponent<mqtt::MQTTBinarySensorComponent, binary_sensor::BinarySensor> {
  using Base = MqttAvailabilityComponent<mqtt::MQTTBinarySensorComponent, binary_sensor::BinarySensor>;

 public:
  explicit CenturionMqttBinarySensor(binary_sensor::BinarySensor *entity)
      : Base(entity, "binary_sensor") {}
};
#endif  // USE_BINARY_SENSOR

#ifdef USE_TEXT_SENSOR
class CenturionMqttTextSensor : public MqttAvailabilityComponent<mqtt::MQTTTextSensor, text_sensor::TextSensor> {
  using Base = MqttAvailabilityComponent<mqtt::MQTTTextSensor, text_sensor::TextSensor>;

 public:
  // The stock MQTTTextSensor reports component_type() "sensor" (Home Assistant
  // has no text_sensor domain); mirror it so topics and unique IDs are
  // unchanged.
  explicit CenturionMqttTextSensor(text_sensor::TextSensor *entity) : Base(entity, "sensor") {}
};
#endif  // USE_TEXT_SENSOR

}  // namespace esphome::centurion_ble

#endif  // USE_MQTT
