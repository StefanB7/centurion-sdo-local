#include "centurion_ble_switch.h"

#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome::centurion_ble {

static const char *const TAG = "centurion_ble.switch";

void CenturionLinkSwitch::setup() {
  if (this->hub_ == nullptr) {
    ESP_LOGE(TAG, "No Centurion hub; check centurion_ble_id");
    return;
  }
  this->hub_->add_on_state_callback(
      [this](const CenturionState &) { this->publish_state(this->hub_->link_enabled()); });
  this->publish_state(this->hub_->link_enabled());
}

void CenturionLinkSwitch::dump_config() { LOG_SWITCH("", "Centurion Link Switch", this); }

void CenturionLinkSwitch::write_state(bool state) {
  if (this->hub_ == nullptr)
    return;
  this->hub_->set_link_enabled(state);
  this->publish_state(state);
}

}  // namespace esphome::centurion_ble

#endif  // USE_ESP32
