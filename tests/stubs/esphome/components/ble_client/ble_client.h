#pragma once

#include <cstdint>
#include <string>

#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

// Minimal ESP-IDF GATT surface used by centurion_ble.cpp.
typedef uint8_t esp_gatt_if_t;

enum esp_gattc_cb_event_t {
  ESP_GATTC_REG_EVT = 0,
  ESP_GATTC_SEARCH_CMPL_EVT,
  ESP_GATTC_REG_FOR_NOTIFY_EVT,
  ESP_GATTC_DISCONNECT_EVT,
  ESP_GATTC_NOTIFY_EVT,
};

enum esp_gatt_status_t {
  ESP_GATT_OK = 0,
};

enum esp_gatt_write_type_t {
  ESP_GATT_WRITE_TYPE_NO_RSP = 0,
};

enum esp_gatt_auth_req_t {
  ESP_GATT_AUTH_REQ_NONE = 0,
};

struct esp_ble_gattc_cb_param_t {
  struct {
    uint16_t handle;
    esp_gatt_status_t status;
  } reg_for_notify;
  struct {
    uint16_t handle;
    uint8_t *value;
    uint16_t value_len;
  } notify;
};

#ifndef ESP_OK
#define ESP_OK 0
#endif

int esp_ble_gattc_write_char(esp_gatt_if_t gattc_if, uint16_t conn_id, uint16_t handle,
                             uint16_t value_len, uint8_t *value,
                             esp_gatt_write_type_t write_type,
                             esp_gatt_auth_req_t auth_req);

namespace esphome::ble_client {

class BLEClient;

class BLEClientNode {
 public:
  virtual ~BLEClientNode() = default;

  virtual void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                   esp_ble_gattc_cb_param_t *param) {
    (void) event;
    (void) gattc_if;
    (void) param;
  }

  esphome::esp32_ble_tracker::ClientState node_state{
      esphome::esp32_ble_tracker::ClientState::INIT};

 protected:
  BLEClient *parent_{nullptr};
};

class BLEClient {
 public:
  struct Characteristic {
    uint16_t handle{0};
  };

  uint64_t get_address() const { return this->address_; }
  void set_address(uint64_t address) { this->address_ = address; }
  const std::string &address_str() const { return this->address_str_; }

  bool connected() const { return this->connected_; }
  void set_connected(bool connected) { this->connected_ = connected; }

  bool enabled() const { return this->enabled_; }
  void set_enabled(bool enabled) { this->enabled_ = enabled; }

  void disconnect() {
    this->connected_ = false;
    this->disconnect_calls++;
  }

  esp_gatt_if_t get_gattc_if() const { return 1; }
  uint16_t get_conn_id() const { return 1; }

  int register_for_notify(uint16_t handle) {
    this->notify_handle = handle;
    return ESP_OK;
  }

  Characteristic *get_characteristic(
      const esphome::esp32_ble_tracker::ESPBTUUID &service,
      const esphome::esp32_ble_tracker::ESPBTUUID &characteristic) {
    (void) service;
    if (characteristic.raw().find("f92c") != std::string::npos)
      return &this->write_characteristic_;
    if (characteristic.raw().find("db3b") != std::string::npos)
      return &this->notify_characteristic_;
    return nullptr;
  }

  Characteristic write_characteristic_{0x0010};
  Characteristic notify_characteristic_{0x0011};
  uint16_t notify_handle{0};
  int disconnect_calls{0};

 protected:
  uint64_t address_{0};
  std::string address_str_{"AA:BB:CC:DD:EE:FF"};
  bool connected_{false};
  bool enabled_{true};
};

}  // namespace esphome::ble_client
