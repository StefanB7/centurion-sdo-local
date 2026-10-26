#include "centurion_ble_binary_sensor.h"

#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome::centurion_ble {

static const char *const TAG = "centurion_ble.binary_sensor";

void CenturionBinarySensor::setup() {
  if (this->hub_ == nullptr) {
    ESP_LOGE(TAG, "No Centurion hub; check centurion_ble_id");
    return;
  }
  this->hub_->add_on_state_callback(
      [this](const CenturionState &state) { this->publish_from_(state); });
  this->publish_from_(this->hub_->state());
}

void CenturionBinarySensor::dump_config() {
  LOG_BINARY_SENSOR("", "Centurion Binary Sensor", this);
}

void CenturionBinarySensor::publish_from_(const CenturionState &state) {
  bool known = false;
  bool value = false;

  switch (this->type_) {
    case CENTURION_BINARY_CONNECTED:
      known = true;
      value = state.connected;
      break;
    case CENTURION_BINARY_MAINS_ON:
      known = state.mains_on_known;
      value = state.mains_on;
      break;
    case CENTURION_BINARY_BATTERY_LOW:
      known = state.battery_low_known;
      value = state.battery_low;
      break;
    case CENTURION_BINARY_COLLISION:
      known = state.collision_known;
      value = state.collision;
      break;
    case CENTURION_BINARY_MAX_COLLISIONS:
      known = state.max_collisions_known;
      value = state.max_collisions;
      break;
    case CENTURION_BINARY_STALLED:
      known = state.stalled_known;
      value = state.stalled;
      break;
    case CENTURION_BINARY_LOST:
      known = state.lost_known;
      value = state.lost;
      break;
    case CENTURION_BINARY_LIMITS_NOT_SET:
      known = state.limits_not_set_known;
      value = state.limits_not_set;
      break;
    case CENTURION_BINARY_XMR_ALARM:
      known = state.xmr_alarm_known;
      value = state.xmr_alarm;
      break;
    default:
      break;
  }

  // Operator fields are only trustworthy while the BLE session is usable. The
  // hub clears the *_known flags on disconnect and on overview expiry; also
  // require connected so a stale flag can never leave a "normal" state behind.
  if (this->type_ != CENTURION_BINARY_CONNECTED)
    known = known && state.connected;

  if (known) {
    this->publish_state(value);
  } else {
    // Clears the state for the native API (missing_state). MQTT has no
    // per-state "unknown", so the entity's YAML availability mapping is what
    // makes Home Assistant show it unavailable when the BLE link is down.
    this->invalidate_state();
  }
}

}  // namespace esphome::centurion_ble

#endif  // USE_ESP32
