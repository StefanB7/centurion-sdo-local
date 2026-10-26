// Host-side checks for components/centurion_ble/mqtt_availability.h.
//
// The MQTT/ArduinoJson headers are replaced by the minimal stubs in
// tests/mqtt_availability_stubs. This compiles the real wrapper header and
// checks the discovery payload shape and delegation to the wrapped ESPHome MQTT
// component.
//
// Build and run from the repository root:
//   g++ -std=c++17 -Wall -Wextra -DUSE_MQTT -DUSE_COVER -DUSE_SENSOR
//       -DUSE_BINARY_SENSOR -DUSE_TEXT_SENSOR -I tests/mqtt_availability_stubs
//       tests/mqtt_availability_test.cpp -o /tmp/centurion_mqtt_availability_test
//   /tmp/centurion_mqtt_availability_test

#include <cstdio>
#include <string>

#include "../components/centurion_ble/mqtt_availability.h"

using namespace esphome;

namespace esphome::mqtt {
MQTTClientComponent *global_mqtt_client = nullptr;
}  // namespace esphome::mqtt

static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                 \
    }                                                             \
  } while (0)

// The wrapped component is protected so production code cannot reach it; expose
// it here to verify delegation.
class TestCover : public centurion_ble::CenturionMqttCover {
 public:
  explicit TestCover(cover::Cover *entity) : CenturionMqttCover(entity) {}
  const mqtt::MQTTCoverComponent &inner() const { return this->inner_; }
};

class TestSensor : public centurion_ble::CenturionMqttSensor {
 public:
  explicit TestSensor(sensor::Sensor *entity) : CenturionMqttSensor(entity) {}
  const mqtt::MQTTSensorComponent &inner() const { return this->inner_; }
};

class TestBinarySensor : public centurion_ble::CenturionMqttBinarySensor {
 public:
  explicit TestBinarySensor(binary_sensor::BinarySensor *entity) : CenturionMqttBinarySensor(entity) {}
  const mqtt::MQTTBinarySensorComponent &inner() const { return this->inner_; }
};

class TestTextSensor : public centurion_ble::CenturionMqttTextSensor {
 public:
  explicit TestTextSensor(text_sensor::TextSensor *entity) : CenturionMqttTextSensor(entity) {}
  const mqtt::MQTTTextSensor &inner() const { return this->inner_; }
};

static JsonValue *member(JsonValue &object, const char *key) {
  auto it = object.members.find(key);
  return it == object.members.end() ? nullptr : &it->second;
}

int main() {
  mqtt::MQTTClientComponent client;
  mqtt::global_mqtt_client = &client;

  cover::Cover cover_entity;
  sensor::Sensor sensor_entity;
  binary_sensor::BinarySensor binary_entity;
  text_sensor::TextSensor text_entity;

  // ---- combined node + operator availability ----
  client.availability = {"centurion-garage/status", "online", "offline"};
  TestCover cover(&cover_entity);
  cover.set_availability("centurion-garage/binary_sensor/ble_connected/state", "ON", "OFF");

  // Base's singular availability slot is deliberately left empty so stock
  // send_discovery_() cannot emit availability_topic next to the list.
  CHECK(cover.single_availability_topic().empty());
  CHECK(cover.single_payload_available().empty());
  CHECK(cover.single_payload_not_available().empty());

  cover.set_qos(1);
  cover.set_retain(false);
  cover.set_subscribe_qos(2);
  cover.set_command_retain(true);
  cover.disable_discovery();
  CHECK(cover.get_qos() == 1);
  CHECK(!cover.get_retain());
  CHECK(cover.get_subscribe_qos() == 2);
  CHECK(cover.get_command_retain());
  CHECK(!cover.get_discovery_enabled());
  CHECK(cover.inner().get_qos() == 1);
  CHECK(!cover.inner().get_retain());
  CHECK(cover.inner().get_subscribe_qos() == 2);
  CHECK(!cover.inner().get_discovery_enabled());

  cover.set_custom_state_topic(std::string("custom/state"));
  cover.set_custom_command_topic(std::string("custom/command"));
  CHECK(cover.get_custom_state_topic() == "custom/state");
  CHECK(cover.get_custom_command_topic() == "custom/command");
  CHECK(cover.inner().get_custom_state_topic() == "custom/state");
  CHECK(cover.inner().get_custom_command_topic() == "custom/command");

  cover.set_custom_position_state_topic("position/state");
  cover.set_custom_position_command_topic("position/command");
  cover.set_custom_tilt_state_topic("tilt/state");
  cover.set_custom_tilt_command_topic("tilt/command");
  CHECK(cover.inner().position_state_topic == "position/state");
  CHECK(cover.inner().position_command_topic == "position/command");
  CHECK(cover.inner().tilt_state_topic == "tilt/state");
  CHECK(cover.inner().tilt_command_topic == "tilt/command");

  cover.setup();
  CHECK(cover.inner().setup_calls == 1);

  JsonValue root_value;
  root_value.type = JsonValue::OBJECT;
  JsonObject root(&root_value);
  mqtt::SendDiscoveryConfig discovery;
  // Dispatch through the MQTTComponent interface, as the real send_discovery_()
  // does.
  mqtt::MQTTComponent *as_mqtt = &cover;
  as_mqtt->send_discovery(root, discovery);

  CHECK(cover.inner().discovery_calls == 1);
  JsonValue *availability = member(root_value, "availability");
  CHECK(availability != nullptr);
  CHECK(availability != nullptr && availability->type == JsonValue::ARRAY);
  CHECK(availability != nullptr && availability->items.size() == 2);
  if (availability != nullptr && availability->items.size() == 2) {
    JsonValue *node_topic = member(availability->items[0], "topic");
    JsonValue *node_available = member(availability->items[0], "payload_available");
    JsonValue *node_unavailable = member(availability->items[0], "payload_not_available");
    CHECK(node_topic != nullptr && node_topic->string == "centurion-garage/status");
    CHECK(node_available != nullptr && node_available->string == "online");
    CHECK(node_unavailable != nullptr && node_unavailable->string == "offline");

    JsonValue *operator_topic = member(availability->items[1], "topic");
    JsonValue *operator_available = member(availability->items[1], "payload_available");
    JsonValue *operator_unavailable = member(availability->items[1], "payload_not_available");
    CHECK(operator_topic != nullptr &&
          operator_topic->string == "centurion-garage/binary_sensor/ble_connected/state");
    CHECK(operator_available != nullptr && operator_available->string == "ON");
    CHECK(operator_unavailable != nullptr && operator_unavailable->string == "OFF");
  }
  JsonValue *mode = member(root_value, "availability_mode");
  CHECK(mode != nullptr && mode->string == "all");

  CHECK(cover.send_initial_state());
  CHECK(cover.inner().initial_state_calls == 1);
  cover.dump_config();
  CHECK(cover.inner().dump_calls == 1);
  CHECK(std::string(as_mqtt->component_type()) == "cover");

  // ---- node availability disabled: operator entry only ----
  client.availability = {"", "", ""};
  TestCover ble_only(&cover_entity);
  ble_only.set_availability("ble/state", "ON", "OFF");
  JsonValue ble_only_root_value;
  ble_only_root_value.type = JsonValue::OBJECT;
  JsonObject ble_only_root(&ble_only_root_value);
  ble_only.send_discovery(ble_only_root, discovery);
  JsonValue *ble_only_availability = member(ble_only_root_value, "availability");
  CHECK(ble_only_availability != nullptr && ble_only_availability->items.size() == 1);
  JsonValue *ble_only_topic = member(ble_only_availability->items[0], "topic");
  CHECK(ble_only_topic != nullptr && ble_only_topic->string == "ble/state");

  // ---- availability explicitly disabled: no array, no mode ----
  TestCover disabled(&cover_entity);
  disabled.set_availability("ble/state", "ON", "OFF");
  disabled.disable_availability();
  JsonValue disabled_root_value;
  disabled_root_value.type = JsonValue::OBJECT;
  JsonObject disabled_root(&disabled_root_value);
  disabled.send_discovery(disabled_root, discovery);
  CHECK(member(disabled_root_value, "availability") == nullptr);
  CHECK(member(disabled_root_value, "availability_mode") == nullptr);

  // ---- all four wrappers compile and keep the stock component_type() ----
  TestSensor sensor(&sensor_entity);
  sensor.set_expire_after(5000);
  CHECK(sensor.inner().get_expire_after() == 5000);
  sensor.disable_expire_after();
  CHECK(sensor.inner().get_expire_after() == 0);
  CHECK(std::string(static_cast<mqtt::MQTTComponent &>(sensor).component_type()) == "sensor");

  TestBinarySensor binary(&binary_entity);
  CHECK(std::string(static_cast<mqtt::MQTTComponent &>(binary).component_type()) == "binary_sensor");

  TestTextSensor text(&text_entity);
  CHECK(std::string(static_cast<mqtt::MQTTComponent &>(text).component_type()) == "sensor");

  if (failures == 0) {
    std::printf("mqtt_availability_test: all checks passed\n");
  }
  return failures == 0 ? 0 : 1;
}
