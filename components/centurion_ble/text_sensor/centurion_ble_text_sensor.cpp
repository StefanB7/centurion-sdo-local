#include "centurion_ble_text_sensor.h"

#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome::centurion_ble {

static const char *const TAG = "centurion_ble.text_sensor";

void CenturionTextSensor::setup() {
  if (this->hub_ == nullptr) {
    ESP_LOGE(TAG, "No Centurion hub; check centurion_ble_id");
    return;
  }
  this->hub_->add_on_state_callback(
      [this](const CenturionState &state) { this->publish_from_(state); });
  this->publish_from_(this->hub_->state());
}

void CenturionTextSensor::dump_config() {
  LOG_TEXT_SENSOR("", "Centurion Text Sensor", this);
}

void CenturionTextSensor::publish_from_(const CenturionState &state) {
  const char *value = "unknown";
  bool known = state.connected;

  switch (this->type_) {
    case CENTURION_TEXT_DOOR_STATUS:
      known = known && state.door_known;
      value = protocol::door_status_name(state.door);
      break;
    case CENTURION_TEXT_POWER_STATUS:
      known = known && state.power_known;
      value = protocol::power_status_name(state.power);
      break;
    default:
      known = false;
      break;
  }

  if (!known) {
    // Keep the initial state absent; after a known value, replace it so the
    // stale operator status is not left behind locally or in the native API.
    if (!this->published_)
      return;
    this->published_ = false;
    this->publish_state("unknown");
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
