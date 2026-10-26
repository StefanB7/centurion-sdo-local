#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"

#include "protocol.h"

#ifdef USE_ESP32

#include <cstdint>
#include <functional>
#include <string>

namespace esphome::centurion_ble {

namespace espbt = esphome::esp32_ble_tracker;

constexpr size_t CENTURION_MAX_PAYLOAD = 32;
constexpr size_t CENTURION_MAX_QUEUE = 8;

/// Latest state learned from the operator. A *_known flag means the field has
/// been populated by a status URC; the value is meaningless until it is true.
struct CenturionState {
  bool connected{false};

  bool door_known{false};
  protocol::DoorStatus door{protocol::DoorStatus::UNRECOGNISED};

  bool battery_known{false};
  float battery_voltage{0.0f};

  bool power_known{false};
  protocol::PowerStatus power{protocol::PowerStatus::UNRECOGNISED};

  bool beam_known{false};
  uint16_t beam_raw{0};

  bool timer_known{false};
  uint16_t timer_remaining{0};

  bool mains_on_known{false};
  bool mains_on{false};

  bool battery_low_known{false};
  bool battery_low{false};

  bool collision_known{false};
  bool collision{false};

  bool max_collisions_known{false};
  bool max_collisions{false};

  bool stalled_known{false};
  bool stalled{false};

  bool lost_known{false};
  bool lost{false};

  bool limits_not_set_known{false};
  bool limits_not_set{false};

  bool xmr_alarm_known{false};
  bool xmr_alarm{false};
};

/// One BLE session to a Centurion SDO4 SMART / SDO5 SMART+ operator.
///
/// This is the hub component. The cover/sensor/binary_sensor/text_sensor/switch
/// platforms are thin views over `CenturionState`; all protocol work happens
/// here so there is exactly one authentication session per operator.
class CenturionBLE : public Component,
                     public ble_client::BLEClientNode,
                     public espbt::ESPBTDeviceListener {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  bool parse_device(const espbt::ESPBTDevice &device) override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_phone(const std::string &phone) { this->phone_ = phone; }
  void set_key(const std::string &hex);
  void set_product_code(uint8_t code) { this->product_code_ = code; }

  // How long to wait for the first status overview after authentication.
  // 0 disables the timeout.
  void set_initial_status_timeout(uint32_t timeout_ms) {
    this->initial_status_timeout_ = timeout_ms;
  }
  uint32_t initial_status_timeout() const { return this->initial_status_timeout_; }

  // How long an overview stays usable for commands and entities. 0 disables
  // freshness checking because the operator's URC cadence is not verified.
  void set_status_timeout(uint32_t timeout_ms) { this->status_timeout_ = timeout_ms; }
  uint32_t status_timeout() const { return this->status_timeout_; }

  const CenturionState &state() const { return this->state_; }
  bool link_enabled() const { return this->link_enabled_; }
  void set_link_enabled(bool enabled);

  /// Age of the last valid overview in milliseconds, or UINT32_MAX if none has
  /// arrived in this session. Unsigned subtraction makes this wrap-safe.
  uint32_t last_status_age() const {
    return this->status_received_ ? millis() - this->last_status_ms_ : UINT32_MAX;
  }

  void open();
  void close();

  template<typename F> void add_on_state_callback(F &&callback) {
    this->state_callback_.add(std::forward<F>(callback));
  }

 protected:
  struct Command {
    uint32_t address{0};
    uint8_t payload[CENTURION_MAX_PAYLOAD]{};
    uint8_t payload_len{0};
    uint32_t timeout_ms{7000};
  };

  enum class SessionState : uint8_t {
    IDLE,
    WAIT_NOTIFY,
    WAIT_DELAY,
    INIT,
    WAIT_STATUS,
    READY,
    DISCONNECTING,
  };

  void publish_state_();
  void update_advertisement_(const uint8_t *data, size_t len);
  void apply_overview_(const protocol::Overview &overview, uint32_t received_ms);

  void start_session_();
  void send_next_();
  void queue_command_(uint32_t address, const uint8_t *payload, size_t payload_len,
                      uint32_t timeout_ms = 7000);
  bool queue_push_(const Command &command);
  bool queue_pop_(Command &command);
  bool write_command_(const Command &command);

  void handle_notification_(const uint8_t *data, size_t len);
  void fail_init_(const char *reason);
  void enter_idle_();
  void enter_wait_status_();
  void begin_reconnect_(const char *reason);
  void reset_session_();
  void expire_state_();
  void mark_disconnected_();
  bool state_fresh_() const;

  // BLEClientNode and ESPBTDeviceListener both declare a protected `parent_`
  // (for the client and the tracker respectively), so an unqualified lookup is
  // ambiguous. This accessor selects the BLE client parent explicitly.
  ble_client::BLEClient *ble_parent_() const {
    return this->ble_client::BLEClientNode::parent_;
  }

  std::string phone_;
  bool key_set_{false};
  uint8_t key_[4]{0, 0, 0, 0};
  uint8_t product_code_{0};
  uint8_t firmware_major_{0};
  uint8_t firmware_minor_{0};

  uint16_t write_handle_{0};
  uint16_t notify_handle_{0};

  SessionState session_state_{SessionState::IDLE};
  uint32_t delay_started_{0};
  uint32_t setup_started_{0};
  uint32_t idle_started_{0};
  uint32_t status_started_{0};
  uint32_t last_status_ms_{0};
  uint32_t initial_status_timeout_{15000};
  uint32_t status_timeout_{0};
  bool status_received_{false};
  bool authenticated_{false};
  bool has_pending_overview_{false};
  protocol::Overview pending_overview_{};
  uint32_t pending_overview_received_ms_{0};
  uint32_t command_started_{0};
  uint32_t command_timeout_{0};
  uint32_t command_address_{0};
  bool command_pending_{false};
  bool disconnect_requested_{false};
  uint8_t init_failures_{0};

  Command queue_[CENTURION_MAX_QUEUE]{};
  size_t queue_head_{0};
  size_t queue_tail_{0};
  size_t queue_count_{0};

  bool link_enabled_{true};
  CenturionState state_;
  CallbackManager<void(const CenturionState &)> state_callback_{};
};

}  // namespace esphome::centurion_ble

#endif  // USE_ESP32
