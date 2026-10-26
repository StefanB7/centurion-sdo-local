#pragma once

// Minimal ESP-IDF GATTC types used by the hub's override signature.
enum esp_gattc_cb_event_t { ESP_GATTC_REG_EVT = 0 };
using esp_gatt_if_t = int;
struct esp_ble_gattc_cb_param_t {};

namespace esphome::ble_client {

class BLEClient {};

class BLEClientNode {
 public:
  virtual ~BLEClientNode() = default;
  virtual void gattc_event_handler(esp_gattc_cb_event_t /*event*/, esp_gatt_if_t /*gattc_if*/,
                                   esp_ble_gattc_cb_param_t * /*param*/) {}

  // The real base has this protected member; the hub's qualified accessor
  // keeps the two listener bases from being ambiguous.
  BLEClient *parent_{nullptr};
};

}  // namespace esphome::ble_client
