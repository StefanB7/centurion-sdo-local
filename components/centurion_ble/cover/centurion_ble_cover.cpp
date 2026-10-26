#include "centurion_ble_cover.h"

#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome::centurion_ble {

static const char *const TAG = "centurion_ble.cover";

void CenturionCover::setup() {
  if (this->hub_ == nullptr) {
    ESP_LOGE(TAG, "No Centurion hub; check centurion_ble_id");
    return;
  }

  this->hub_->add_on_state_callback(
      [this](const CenturionState &state) { this->apply_state_(state); });

  // ESPHome has no "unknown" cover position. 0.5 makes the MQTT/API cover state
  // "unknown" until the first status URC arrives (the MQTT cover maps a
  // non-0/1 position without position support to "unknown").
  this->position = 0.5f;
  this->current_operation = cover::COVER_OPERATION_IDLE;
  this->publish_state(false);

  this->apply_state_(this->hub_->state());
}

void CenturionCover::dump_config() {
  LOG_COVER("", "Centurion Cover", this);
}

cover::CoverTraits CenturionCover::get_traits() {
  cover::CoverTraits traits;
  traits.set_supports_stop(false);
  traits.set_supports_position(false);
  traits.set_supports_tilt(false);
  traits.set_is_assumed_state(false);
  return traits;
}

void CenturionCover::control(const cover::CoverCall &call) {
  if (this->hub_ == nullptr)
    return;

  const auto position = call.get_position();
  if (position.has_value()) {
    if (*position >= 0.5f)
      this->hub_->open();
    else
      this->hub_->close();
  }
}

void CenturionCover::apply_state_(const CenturionState &state) {
  if (!state.connected || !state.door_known) {
    if (this->has_door_) {
      this->has_door_ = false;
      this->last_door_ = protocol::DoorStatus::UNRECOGNISED;
      this->position = 0.5f;
      this->current_operation = cover::COVER_OPERATION_IDLE;
      this->publish_state(false);
    }
    return;
  }
  if (this->has_door_ && this->last_door_ == state.door)
    return;

  this->has_door_ = true;
  this->last_door_ = state.door;

  switch (state.door) {
    case protocol::DoorStatus::OPEN:
      this->position = cover::COVER_OPEN;
      this->current_operation = cover::COVER_OPERATION_IDLE;
      break;
    case protocol::DoorStatus::CLOSED:
      this->position = cover::COVER_CLOSED;
      this->current_operation = cover::COVER_OPERATION_IDLE;
      break;
    case protocol::DoorStatus::OPENING:
      this->position = cover::COVER_OPEN;
      this->current_operation = cover::COVER_OPERATION_OPENING;
      break;
    case protocol::DoorStatus::CLOSING:
      this->position = cover::COVER_CLOSED;
      this->current_operation = cover::COVER_OPERATION_CLOSING;
      break;
    default:
      // partially_open / learn / lost / unknown all have no binary position.
      this->position = 0.5f;
      this->current_operation = cover::COVER_OPERATION_IDLE;
      break;
  }

  this->publish_state(false);
}

}  // namespace esphome::centurion_ble

#endif  // USE_ESP32
