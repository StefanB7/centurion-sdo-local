#include "centurion_ble_sensor.h"

#include <cmath>

#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome::centurion_ble {

static const char *const TAG = "centurion_ble.sensor";

void CenturionSensor::setup() {
  if (this->hub_ == nullptr) {
    ESP_LOGE(TAG, "No Centurion hub; check centurion_ble_id");
    return;
  }
  this->hub_->add_on_state_callback(
      [this](const CenturionState &state) { this->publish_from_(state); });
  this->publish_from_(this->hub_->state());
}

void CenturionSensor::dump_config() { LOG_SENSOR("", "Centurion Sensor", this); }

void CenturionSensor::publish_from_(const CenturionState &state) {
  float value = NAN;
  bool known = state.connected;

  switch (this->type_) {
    case CENTURION_SENSOR_BATTERY_VOLTAGE:
      known = known && state.battery_known;
      value = state.battery_voltage;
      break;
    case CENTURION_SENSOR_TIMER_REMAINING:
      known = known && state.timer_known;
      value = static_cast<float>(state.timer_remaining);
      break;
    case CENTURION_SENSOR_BEAM_RAW:
      known = known && state.beam_known;
      value = static_cast<float>(state.beam_raw);
      break;
    default:
      known = false;
      break;
  }

  if (!known) {
    // Leave the entity without a state until the first valid overview so a boot
    // does not publish a meaningless NaN. Once a value was shown, publish NaN
    // so native API clients do not keep showing the stale operator value.
    if (!this->published_)
      return;
    this->published_ = false;
    this->publish_state(NAN);
    return;
  }

  // The operator repeats its overview; only publish (and so send MQTT) on change.
  if (this->published_ && this->state == value)
    return;
  this->published_ = true;
  this->publish_state(value);
}

}  // namespace esphome::centurion_ble

#endif  // USE_ESP32
