// Host-side checks for the Centurion entity visibility rules.
//
// These tests compile the real platform .cpp files against the minimal ESPHome
// stubs in tests/entity_stubs, so the state/invalidation logic is exercised
// without the ESP-IDF BLE stack.
//
/*
 * Build and run from the repository root:
 *   g++ -std=c++17 -DUSE_ESP32 -I tests/entity_stubs \
 *       tests/entity_test.cpp \
 *       components/centurion_ble/binary_sensor/centurion_ble_binary_sensor.cpp \
 *       components/centurion_ble/sensor/centurion_ble_sensor.cpp \
 *       components/centurion_ble/text_sensor/centurion_ble_text_sensor.cpp \
 *       components/centurion_ble/cover/centurion_ble_cover.cpp \
 *       components/centurion_ble/protocol.cpp \
 *       -o /tmp/centurion_entity_test
 *   /tmp/centurion_entity_test
 */

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "../components/centurion_ble/binary_sensor/centurion_ble_binary_sensor.h"
#include "../components/centurion_ble/cover/centurion_ble_cover.h"
#include "../components/centurion_ble/sensor/centurion_ble_sensor.h"
#include "../components/centurion_ble/text_sensor/centurion_ble_text_sensor.h"

using namespace esphome;
using namespace esphome::centurion_ble;

static int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
      failures++;                                                         \
    }                                                                     \
  } while (0)

#define CHECK_NEAR(actual, expected)                                                \
  do {                                                                              \
    if (std::fabs((actual) - (expected)) > 0.001f) {                                \
      std::printf("FAIL %s:%d: %s == %f, expected %f\n", __FILE__, __LINE__, #actual, \
                  static_cast<double>(actual), static_cast<double>(expected));      \
      failures++;                                                                   \
    }                                                                               \
  } while (0)

// The hub's real state_/publish_state_ are protected, which lets the test drive
// the same callback path the BLE session uses.
class TestHub : public CenturionBLE {
 public:
  void publish(const CenturionState &state) {
    this->state_ = state;
    this->publish_state_();
  }
};

// Definitions for the hub overrides declared in centurion_ble.h. The test only
// needs the object and its callback manager, not the BLE implementation.
namespace esphome::centurion_ble {

void CenturionBLE::setup() {}
void CenturionBLE::loop() {}
void CenturionBLE::dump_config() {}
bool CenturionBLE::parse_device(const espbt::ESPBTDevice & /*device*/) { return false; }
void CenturionBLE::gattc_event_handler(esp_gattc_cb_event_t /*event*/, esp_gatt_if_t /*gattc_if*/,
                                       esp_ble_gattc_cb_param_t * /*param*/) {}

// Matches the real hub: publishing state simply notifies all view callbacks.
// The cover references these command methods, so they need definitions too.
void CenturionBLE::publish_state_() { this->state_callback_.call(this->state_); }
void CenturionBLE::open() {}
void CenturionBLE::close() {}

}  // namespace esphome::centurion_ble

static CenturionState all_known_state() {
  CenturionState state;
  state.connected = true;

  state.door_known = true;
  state.door = protocol::DoorStatus::OPEN;

  state.battery_known = true;
  state.battery_voltage = 12.8f;

  state.power_known = true;
  state.power = protocol::PowerStatus::NORMAL;

  state.beam_known = true;
  state.beam_raw = 321;

  state.timer_known = true;
  state.timer_remaining = 45;

  state.mains_on_known = true;
  state.mains_on = true;

  state.battery_low_known = true;
  state.battery_low = true;

  state.collision_known = true;
  state.collision = true;

  state.max_collisions_known = true;
  state.max_collisions = true;

  state.stalled_known = true;
  state.stalled = true;

  state.lost_known = true;
  state.lost = true;

  state.limits_not_set_known = true;
  state.limits_not_set = true;

  state.xmr_alarm_known = true;
  state.xmr_alarm = true;

  return state;
}

// Stale values from before a disconnect must never be trusted, even if a
// *_known flag was left behind.
static CenturionState stale_known_but_disconnected_state() {
  CenturionState state;
  state.connected = false;

  state.door_known = true;
  state.door = protocol::DoorStatus::CLOSED;

  state.battery_known = true;
  state.battery_voltage = 13.1f;

  state.mains_on_known = true;
  state.mains_on = true;

  return state;
}

static void setup_binary_sensor(CenturionBinarySensor *sensor, CenturionBLE *hub, uint8_t type) {
  sensor->set_hub(hub);
  sensor->set_type(type);
  sensor->setup();
}

int main() {
  TestHub hub;

  std::array<CenturionBinarySensor, 9> binary_sensors;
  for (size_t index = 0; index < binary_sensors.size(); index++)
    setup_binary_sensor(&binary_sensors[index], &hub, static_cast<uint8_t>(index));

  CenturionSensor battery;
  battery.set_hub(&hub);
  battery.set_type(CENTURION_SENSOR_BATTERY_VOLTAGE);
  battery.setup();

  CenturionTextSensor door_text;
  door_text.set_hub(&hub);
  door_text.set_type(CENTURION_TEXT_DOOR_STATUS);
  door_text.setup();

  CenturionCover cover_entity;
  cover_entity.set_hub(&hub);
  cover_entity.setup();

  // --- Initial absent states -------------------------------------------------
  // The status source reports OFF at boot so MQTT availability starts as
  // unavailable; every operator-dependent entity must have no state at all.
  CHECK(binary_sensors[0].has_state());
  CHECK(!binary_sensors[0].state);
  for (size_t index = 1; index < binary_sensors.size(); index++)
    CHECK(!binary_sensors[index].has_state());

  CHECK(!battery.has_state());
  CHECK(!door_text.has_state());

  // ESPHome has no "unknown" cover position; 0.5 maps to unknown on the wire.
  CHECK_NEAR(cover_entity.position, 0.5f);
  CHECK(cover_entity.current_operation == cover::COVER_OPERATION_IDLE);
  CHECK(cover_entity.publish_count == 1);

  // --- Connected overview publishes every field ------------------------------
  hub.publish(all_known_state());

  for (size_t index = 0; index < binary_sensors.size(); index++) {
    CHECK(binary_sensors[index].has_state());
    CHECK(binary_sensors[index].state);
  }
  CHECK(battery.has_state());
  CHECK_NEAR(battery.state, 12.8f);
  CHECK(door_text.has_state());
  CHECK(door_text.state == "open");
  CHECK_NEAR(cover_entity.position, cover::COVER_OPEN);
  CHECK(cover_entity.current_operation == cover::COVER_OPERATION_IDLE);

  // --- Known -> disconnected invalidates everything --------------------------
  // The hub resets connected and all *_known flags, then publishes once.
  hub.publish(CenturionState{});

  // The status entity must stay available so it can report the OFF state.
  CHECK(binary_sensors[0].has_state());
  CHECK(!binary_sensors[0].state);
  for (size_t index = 1; index < binary_sensors.size(); index++)
    CHECK(!binary_sensors[index].has_state());

  CHECK(battery.has_state());
  CHECK(std::isnan(battery.state));
  CHECK(door_text.has_state());
  CHECK(door_text.state == "unknown");
  CHECK_NEAR(cover_entity.position, 0.5f);
  CHECK(cover_entity.current_operation == cover::COVER_OPERATION_IDLE);

  // --- Connected guard beats stale *_known flags -----------------------------
  CenturionBinarySensor stale_mains;
  setup_binary_sensor(&stale_mains, &hub, CENTURION_BINARY_MAINS_ON);

  CenturionSensor stale_battery;
  stale_battery.set_hub(&hub);
  stale_battery.set_type(CENTURION_SENSOR_BATTERY_VOLTAGE);
  stale_battery.setup();

  CenturionTextSensor stale_door;
  stale_door.set_hub(&hub);
  stale_door.set_type(CENTURION_TEXT_DOOR_STATUS);
  stale_door.setup();

  CenturionCover stale_cover;
  stale_cover.set_hub(&hub);
  stale_cover.setup();

  // No prior state: stale known flags while disconnected must stay absent.
  hub.publish(stale_known_but_disconnected_state());
  CHECK(!stale_mains.has_state());
  CHECK(!stale_battery.has_state());
  CHECK(!stale_door.has_state());
  CHECK_NEAR(stale_cover.position, 0.5f);

  // Prior state: a later "disconnected but flag still true" update must clear
  // the old value instead of leaving the last normal reading in place.
  hub.publish(all_known_state());
  CHECK(stale_mains.has_state());
  CHECK(stale_mains.state);
  CHECK_NEAR(stale_battery.state, 12.8f);
  CHECK(stale_door.state == "open");
  CHECK_NEAR(stale_cover.position, cover::COVER_OPEN);

  hub.publish(stale_known_but_disconnected_state());
  CHECK(!stale_mains.has_state());
  CHECK(std::isnan(stale_battery.state));
  CHECK(stale_door.state == "unknown");
  CHECK_NEAR(stale_cover.position, 0.5f);

  // --- Reconnect with the same value must republish --------------------------
  CenturionBinarySensor reconnect_mains;
  setup_binary_sensor(&reconnect_mains, &hub, CENTURION_BINARY_MAINS_ON);
  int mains_events = 0;
  bool mains_last = false;
  reconnect_mains.add_on_state_callback([&](bool value) {
    mains_events++;
    mains_last = value;
  });

  CenturionSensor reconnect_battery;
  reconnect_battery.set_hub(&hub);
  reconnect_battery.set_type(CENTURION_SENSOR_BATTERY_VOLTAGE);
  reconnect_battery.setup();
  int battery_events = 0;
  float battery_last = NAN;
  reconnect_battery.add_on_state_callback([&](float value) {
    battery_events++;
    battery_last = value;
  });

  CenturionTextSensor reconnect_door;
  reconnect_door.set_hub(&hub);
  reconnect_door.set_type(CENTURION_TEXT_DOOR_STATUS);
  reconnect_door.setup();
  int door_events = 0;
  std::string door_last;
  reconnect_door.add_on_state_callback([&](const std::string &value) {
    door_events++;
    door_last = value;
  });

  CenturionCover reconnect_cover;
  reconnect_cover.set_hub(&hub);
  reconnect_cover.setup();

  hub.publish(all_known_state());
  CHECK(mains_events == 1);
  CHECK(mains_last);
  CHECK(battery_events == 1);
  CHECK_NEAR(battery_last, 12.8f);
  CHECK(door_events == 1);
  CHECK(door_last == "open");
  CHECK_NEAR(reconnect_cover.position, cover::COVER_OPEN);
  CHECK(reconnect_cover.publish_count == 2);

  hub.publish(CenturionState{});
  CHECK(mains_events == 1);
  CHECK(!reconnect_mains.has_state());
  CHECK(battery_events == 2);
  CHECK(std::isnan(battery_last));
  CHECK(door_events == 2);
  CHECK(door_last == "unknown");
  CHECK(reconnect_cover.publish_count == 3);
  CHECK_NEAR(reconnect_cover.position, 0.5f);

  // Reconnect delivers the exact same values as before; they must still reach
  // the frontends now that the previous state was invalidated/cleared.
  hub.publish(all_known_state());
  CHECK(mains_events == 2);
  CHECK(mains_last);
  CHECK(reconnect_mains.has_state());
  CHECK(battery_events == 3);
  CHECK_NEAR(battery_last, 12.8f);
  CHECK(!std::isnan(reconnect_battery.state));
  CHECK(door_events == 3);
  CHECK(door_last == "open");
  CHECK(reconnect_cover.publish_count == 4);
  CHECK_NEAR(reconnect_cover.position, cover::COVER_OPEN);

  if (failures == 0)
    std::printf("All entity availability tests passed\n");
  return failures == 0 ? 0 : 1;
}
