#include "centurion_ble.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"

#include <cstring>
#include <vector>

namespace esphome::centurion_ble {

static const char *const TAG = "centurion_ble";

void CenturionBLE::setup() {
  // BLEClientNode::node_state has no default initializer; start from INIT so
  // all_nodes_established_() cannot see a stale value before notify registration.
  this->node_state = espbt::ClientState::INIT;

  ESP_LOGCONFIG(TAG, "Centurion BLE:");
  ESP_LOGCONFIG(TAG, "  Product: %s", protocol::product_name(this->product_code_));
  ESP_LOGCONFIG(TAG, "  Phone: configured (%u characters)", static_cast<unsigned>(this->phone_.size()));
  ESP_LOGCONFIG(TAG, "  Key: %s", this->key_set_ ? "configured" : "captured from advertisement");
  ESP_LOGCONFIG(TAG, "  BLE link: %s", this->link_enabled_ ? "enabled" : "disabled");
  if (this->ble_parent_() == nullptr) {
    ESP_LOGE(TAG, "No ble_client parent; check ble_client_id");
  }
}

void CenturionBLE::dump_config() {
  ESP_LOGCONFIG(TAG, "Centurion BLE session:");
  ESP_LOGCONFIG(TAG, "  Operator MAC: %s",
                this->ble_parent_() != nullptr ? this->ble_parent_()->address_str() : "unknown");
  ESP_LOGCONFIG(TAG, "  Product: %s", protocol::product_name(this->product_code_));
  ESP_LOGCONFIG(TAG, "  Firmware: %u.%u", this->firmware_major_, this->firmware_minor_);
  ESP_LOGCONFIG(TAG, "  Initial status timeout: %u ms",
                static_cast<unsigned>(this->initial_status_timeout_));
  ESP_LOGCONFIG(TAG, "  Status timeout: %u ms%s", static_cast<unsigned>(this->status_timeout_),
                this->status_timeout_ == 0 ? " (disabled)" : "");
  ESP_LOGCONFIG(TAG, "  Key source: %s", this->key_set_ ? "configured/captured" : "waiting");
  ESP_LOGCONFIG(TAG, "  Session: %s", this->state_.connected ? "ready" : "not ready");
}

void CenturionBLE::set_key(const std::string &hex) {
  std::string cleaned;
  cleaned.reserve(hex.size());
  for (char c : hex) {
    if (c != ':' && c != '-' && c != ' ')
      cleaned.push_back(c);
  }

  auto hex_value = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };

  if (cleaned.size() != 8) {
    ESP_LOGE(TAG, "key must be exactly four bytes (8 hex characters)");
    return;
  }

  uint8_t key[4];
  for (size_t i = 0; i < 4; i++) {
    const int high = hex_value(cleaned[i * 2]);
    const int low = hex_value(cleaned[i * 2 + 1]);
    if (high < 0 || low < 0) {
      ESP_LOGE(TAG, "key contains a non-hexadecimal character");
      return;
    }
    key[i] = static_cast<uint8_t>((high << 4) | low);
  }

  std::memcpy(this->key_, key, sizeof(this->key_));
  this->key_set_ = true;
  ESP_LOGI(TAG, "Using the authentication key configured in YAML");
}

bool CenturionBLE::parse_device(const espbt::ESPBTDevice &device) {
  if (this->ble_parent_() == nullptr)
    return false;
  if (device.address_uint64() != this->ble_parent_()->get_address())
    return false;

  // The operator splits its payload across two manufacturer-data entries
  // (serials under one company ID, key/state/firmware/product under another).
  // The app's 18- or 20-byte array is those entries joined in advertised order.
  std::vector<uint8_t> joined;
  for (const auto &manufacturer_data : device.get_manufacturer_datas())
    joined.insert(joined.end(), manufacturer_data.data.begin(), manufacturer_data.data.end());
  if (joined.size() == 18 || joined.size() == 20)
    this->update_advertisement_(joined.data(), joined.size());
  return false;
}

void CenturionBLE::update_advertisement_(const uint8_t *data, size_t len) {
  protocol::Advertisement advertisement;
  if (!protocol::parse_advertisement(data, len, advertisement))
    return;
  if (!protocol::is_supported_product(advertisement.product_code)) {
    ESP_LOGW(TAG, "Ignoring advertisement with unsupported product code %u",
             advertisement.product_code);
    return;
  }

  const bool key_changed =
      !this->key_set_ || std::memcmp(this->key_, advertisement.key, sizeof(this->key_)) != 0;
  std::memcpy(this->key_, advertisement.key, sizeof(this->key_));
  this->key_set_ = true;

  if (key_changed)
    ESP_LOGI(TAG, "Authentication key captured from advertisement");
  if (this->product_code_ != advertisement.product_code) {
    this->product_code_ = advertisement.product_code;
    ESP_LOGI(TAG, "Detected %s", protocol::product_name(advertisement.product_code));
  }
  this->firmware_major_ = advertisement.firmware_major;
  this->firmware_minor_ = advertisement.firmware_minor;
}

void CenturionBLE::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                       esp_ble_gattc_cb_param_t *param) {
  (void) gattc_if;
  // A disabled link or a session that is tearing down must not let late GATT
  // callbacks rebuild state. The disconnect event is always processed so the
  // state machine can return to IDLE.
  if (!this->link_enabled_ && event != ESP_GATTC_DISCONNECT_EVT)
    return;

  switch (event) {
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      if (this->ble_parent_() == nullptr || !this->ble_parent_()->connected())
        break;

      // Only the first discovery of a connection starts a session. Discovery
      // from a superseded connection must not reset an active session.
      if (this->session_state_ != SessionState::IDLE)
        break;

      const auto service = espbt::ESPBTUUID::from_raw(protocol::SERVICE_UUID);
      auto *write_characteristic = this->ble_parent_()->get_characteristic(
          service, espbt::ESPBTUUID::from_raw(protocol::WRITE_UUID));
      auto *notify_characteristic = this->ble_parent_()->get_characteristic(
          service, espbt::ESPBTUUID::from_raw(protocol::NOTIFY_UUID));

      if (write_characteristic == nullptr || notify_characteristic == nullptr) {
        ESP_LOGE(TAG, "Centurion serial service/characteristics not found");
        this->fail_init_("serial service not found");
        break;
      }

      this->write_handle_ = write_characteristic->handle;
      this->notify_handle_ = notify_characteristic->handle;
      this->setup_started_ = millis();
      this->session_state_ = SessionState::WAIT_NOTIFY;

      const auto status = this->ble_parent_()->register_for_notify(this->notify_handle_);
      if (status != ESP_OK) {
        ESP_LOGW(TAG, "register_for_notify failed: %d", static_cast<int>(status));
        this->fail_init_("notify registration failed");
      }
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      if (this->session_state_ != SessionState::WAIT_NOTIFY)
        break;
      if (param->reg_for_notify.handle != this->notify_handle_)
        break;
      if (param->reg_for_notify.status != ESP_GATT_OK) {
        ESP_LOGE(TAG, "Notify registration failed: %d",
                 static_cast<int>(param->reg_for_notify.status));
        this->fail_init_("notify registration failed");
        break;
      }

      this->node_state = espbt::ClientState::ESTABLISHED;
      this->session_state_ = SessionState::WAIT_DELAY;
      this->delay_started_ = millis();
      ESP_LOGD(TAG, "Notify registered; waiting 1.5 s before authentication");
      break;
    }
    case ESP_GATTC_DISCONNECT_EVT:
      ESP_LOGI(TAG, "Disconnected from operator");
      this->reset_session_();
      this->mark_disconnected_();
      break;
    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle != this->notify_handle_)
        break;
      this->handle_notification_(param->notify.value, param->notify.value_len);
      break;
    default:
      break;
  }
}

void CenturionBLE::loop() {
  if (this->disconnect_requested_) {
    this->disconnect_requested_ = false;
    if (this->ble_parent_() != nullptr)
      this->ble_parent_()->disconnect();
    return;
  }

  if (!this->link_enabled_)
    return;

  if (this->session_state_ == SessionState::WAIT_NOTIFY) {
    if (millis() - this->setup_started_ > 10000) {
      ESP_LOGW(TAG, "Notification setup timed out; reconnecting");
      this->fail_init_("notify setup timeout");
    }
    return;
  }

  if (this->session_state_ == SessionState::WAIT_DELAY) {
    if (millis() - this->delay_started_ >= 1500)
      this->start_session_();
    else
      return;
  }

  if (this->session_state_ == SessionState::IDLE) {
    if (this->ble_parent_() != nullptr && this->ble_parent_()->connected()) {
      // Handles are only set by a completed service discovery. Starting the
      // session before that would write to handle 0.
      if (this->write_handle_ == 0 || this->notify_handle_ == 0)
        return;
      if (this->key_set_ && !this->phone_.empty()) {
        this->start_session_();
      } else if (millis() - this->idle_started_ > 15000) {
        ESP_LOGW(TAG, "No authentication key or phone yet; releasing the BLE link");
        this->ble_parent_()->disconnect();
      }
    }
    return;
  }

  if (this->session_state_ == SessionState::WAIT_STATUS) {
    if (this->initial_status_timeout_ > 0 &&
        millis() - this->status_started_ > this->initial_status_timeout_) {
      ESP_LOGW(TAG, "No status overview within %u ms; reconnecting",
               static_cast<unsigned>(this->initial_status_timeout_));
      this->fail_init_("status overview timeout");
    }
    return;
  }

  if (this->session_state_ != SessionState::INIT && this->session_state_ != SessionState::READY)
    return;

  if (this->command_pending_ &&
      (millis() - this->command_started_) > this->command_timeout_) {
    ESP_LOGW(TAG, "Command 0x%08X timed out",
             static_cast<unsigned>(this->command_address_));
    this->command_pending_ = false;
    if (this->session_state_ == SessionState::INIT) {
      this->fail_init_("command timeout");
      return;
    }

    // In READY the reply stream has no transaction identifier. Continuing
    // after a local timeout could attribute a late completion to the next
    // command, so drop queued motion requests and establish a fresh session.
    // Queued commands are never replayed.
    this->begin_reconnect_("command timeout");
    return;
  }

  if (this->session_state_ == SessionState::READY && !this->state_fresh_()) {
    this->begin_reconnect_("status expired");
    return;
  }

  if (!this->command_pending_ && this->queue_count_ > 0)
    this->send_next_();

  // Authentication is only half the session. The first valid status overview
  // promotes WAIT_STATUS to READY; reaching INIT completion alone must not
  // advertise a usable connection.
  if (this->session_state_ == SessionState::INIT && !this->command_pending_ &&
      this->queue_count_ == 0 && this->ble_parent_() != nullptr &&
      this->ble_parent_()->connected()) {
    this->enter_wait_status_();
  }
}

void CenturionBLE::start_session_() {
  if (!this->key_set_) {
    ESP_LOGE(TAG, "No authentication key yet; waiting for an advertisement");
    this->enter_idle_();
    return;
  }
  if (this->phone_.empty()) {
    ESP_LOGE(TAG, "No registered phone number configured");
    this->enter_idle_();
    return;
  }

  this->queue_head_ = 0;
  this->queue_tail_ = 0;
  this->queue_count_ = 0;
  this->command_pending_ = false;
  this->status_received_ = false;
  this->authenticated_ = false;
  this->has_pending_overview_ = false;
  this->pending_overview_received_ms_ = 0;
  this->session_state_ = SessionState::INIT;

  // The app writes 10, 12, 0, 4000 as four big-endian uint16 values.
  const uint8_t connection_parameters[] = {0x00, 0x0A, 0x00, 0x0C,
                                           0x00, 0x00, 0x0F, 0xA0};
  this->queue_command_(protocol::CMD_CHANGE_BLE_CONNECTION_PARAMETERS,
                       connection_parameters, sizeof(connection_parameters));

  uint8_t encoded_phone[64];
  const size_t encoded_len = protocol::encode_phone(this->phone_.c_str(), this->key_,
                                                    encoded_phone, sizeof(encoded_phone));
  if (encoded_len == 0) {
    ESP_LOGE(TAG, "Could not encode the registered phone number");
    this->enter_idle_();
    return;
  }
  this->queue_command_(protocol::CMD_AUTH_MOBILE_PHONE_A, encoded_phone, encoded_len);
  this->queue_command_(protocol::CMD_DISABLE_URC_BLOCK, nullptr, 0);
  this->queue_command_(protocol::CMD_ENTER_STATUS_OVERVIEW, nullptr, 0);

  ESP_LOGD(TAG, "Starting authentication sequence");
}

void CenturionBLE::send_next_() {
  if (this->ble_parent_() == nullptr || !this->ble_parent_()->connected()) {
    ESP_LOGW(TAG, "Dropping queued command: link is not connected");
    this->queue_head_ = 0;
    this->queue_tail_ = 0;
    this->queue_count_ = 0;
    this->command_pending_ = false;
    if (this->session_state_ == SessionState::INIT)
      this->fail_init_("link down");
    else
      this->begin_reconnect_("link down");
    return;
  }

  Command command;
  if (!this->queue_pop_(command))
    return;

  if (!this->write_command_(command)) {
    if (this->session_state_ == SessionState::INIT)
      this->fail_init_("write failed");
    else
      this->begin_reconnect_("write failed");
    return;
  }

  this->command_pending_ = true;
  this->command_started_ = millis();
  this->command_timeout_ = command.timeout_ms;
  this->command_address_ = command.address;
}

bool CenturionBLE::write_command_(const Command &command) {
  uint8_t frame[10 + CENTURION_MAX_PAYLOAD];
  const size_t frame_len = protocol::build_command(
      command.address, command.payload, command.payload_len, frame, sizeof(frame));
  if (frame_len == 0) {
    ESP_LOGE(TAG, "Could not build command 0x%08X",
             static_cast<unsigned>(command.address));
    return false;
  }

  const auto status = esp_ble_gattc_write_char(
      this->ble_parent_()->get_gattc_if(), this->ble_parent_()->get_conn_id(), this->write_handle_,
      static_cast<uint16_t>(frame_len), frame, ESP_GATT_WRITE_TYPE_NO_RSP,
      ESP_GATT_AUTH_REQ_NONE);
  if (status != ESP_OK) {
    ESP_LOGW(TAG, "Write of 0x%08X failed: %d", static_cast<unsigned>(command.address),
             static_cast<int>(status));
    return false;
  }
  return true;
}

void CenturionBLE::queue_command_(uint32_t address, const uint8_t *payload,
                                  size_t payload_len, uint32_t timeout_ms) {
  if (payload_len > CENTURION_MAX_PAYLOAD) {
    ESP_LOGE(TAG, "Command 0x%08X payload is too large", static_cast<unsigned>(address));
    return;
  }

  Command command;
  command.address = address;
  command.payload_len = static_cast<uint8_t>(payload_len);
  command.timeout_ms = timeout_ms;
  if (payload_len > 0 && payload != nullptr)
    std::memcpy(command.payload, payload, payload_len);

  if (!this->queue_push_(command)) {
    ESP_LOGE(TAG, "Command queue full; dropping 0x%08X", static_cast<unsigned>(address));
  }
}

bool CenturionBLE::queue_push_(const Command &command) {
  if (this->queue_count_ >= CENTURION_MAX_QUEUE)
    return false;
  this->queue_[this->queue_tail_] = command;
  this->queue_tail_ = (this->queue_tail_ + 1) % CENTURION_MAX_QUEUE;
  this->queue_count_++;
  return true;
}

bool CenturionBLE::queue_pop_(Command &command) {
  if (this->queue_count_ == 0)
    return false;
  command = this->queue_[this->queue_head_];
  this->queue_head_ = (this->queue_head_ + 1) % CENTURION_MAX_QUEUE;
  this->queue_count_--;
  return true;
}

void CenturionBLE::handle_notification_(const uint8_t *data, size_t len) {
  // Notifications are only valid inside a session that has an authenticated
  // link. After a disconnect or link-disable the handle is cleared and the
  // state leaves READY, so late replies are
  // dropped instead of mutating the next session.
  if (this->notify_handle_ == 0 || !this->link_enabled_ ||
      (this->session_state_ != SessionState::INIT &&
       this->session_state_ != SessionState::WAIT_STATUS &&
       this->session_state_ != SessionState::READY)) {
    ESP_LOGD(TAG, "Ignoring notification outside an active session");
    return;
  }

  ESP_LOGD(TAG, "Centurion RX: %s", format_hex_pretty(data, len).c_str());

  const protocol::Notification notification = protocol::decode_notification(data, len);

  switch (notification.kind) {
    case protocol::NotificationKind::COMPLETE:
      if (!this->command_pending_) {
        ESP_LOGD(TAG, "Ignoring completion with no pending command");
        break;
      }
      this->command_pending_ = false;
      if (this->session_state_ == SessionState::INIT &&
          this->command_address_ == protocol::CMD_AUTH_MOBILE_PHONE_A)
        this->authenticated_ = true;
      if (this->session_state_ == SessionState::INIT && this->queue_count_ == 0)
        this->enter_wait_status_();
      break;
    case protocol::NotificationKind::NACK:
    case protocol::NotificationKind::TIMEOUT:
      if (!this->command_pending_) {
        ESP_LOGD(TAG, "Ignoring error reply with no pending command");
        break;
      }
      ESP_LOGW(TAG, "Command 0x%08X was %s", static_cast<unsigned>(this->command_address_),
               notification.kind == protocol::NotificationKind::NACK ? "rejected" : "timed out");
      this->command_pending_ = false;
      if (this->session_state_ == SessionState::INIT)
        this->fail_init_(notification.kind == protocol::NotificationKind::NACK ? "NACK"
                                                                               : "controller timeout");
      break;
    case protocol::NotificationKind::URC:
      if (!notification.has_overview)
        break;
      if (this->session_state_ == SessionState::INIT) {
        // Before authentication an overview may be stale or unrelated. Once
        // AuthMobilePhoneA completed it is trusted, but is held until the init
        // sequence finishes so a URC that beat the final completion is not
        // lost.
        if (this->authenticated_) {
          this->pending_overview_ = notification.overview;
          this->pending_overview_received_ms_ = millis();
          this->has_pending_overview_ = true;
        }
        break;
      }
      this->apply_overview_(notification.overview, millis());
      break;
    default:
      break;
  }
}

void CenturionBLE::apply_overview_(const protocol::Overview &overview, uint32_t received_ms) {
  // An overview is only trusted after the authentication sequence completed.
  // URCs seen during INIT may predate authentication and must not mark the
  // session connected.
  if (this->session_state_ != SessionState::WAIT_STATUS &&
      this->session_state_ != SessionState::READY) {
    ESP_LOGD(TAG, "Ignoring overview before authentication completed");
    return;
  }

  if (this->session_state_ == SessionState::WAIT_STATUS) {
    this->session_state_ = SessionState::READY;
    this->init_failures_ = 0;
    ESP_LOGI(TAG, "Centurion session ready");
  }

  this->state_.connected = true;
  this->status_received_ = true;
  this->last_status_ms_ = received_ms;

  this->state_.door_known = true;
  this->state_.door = overview.door;

  this->state_.battery_known = true;
  this->state_.battery_voltage = static_cast<float>(overview.battery_raw) / 10.0f;

  this->state_.power_known = true;
  this->state_.power = overview.power;

  this->state_.beam_known = true;
  this->state_.beam_raw = overview.beam_raw;

  this->state_.timer_known = true;
  this->state_.timer_remaining = overview.seconds_remaining;

  this->state_.mains_on_known = true;
  this->state_.mains_on = overview.mains_on;

  this->state_.battery_low_known = true;
  this->state_.battery_low = overview.battery_low;

  this->state_.collision_known = true;
  this->state_.collision = overview.collision;

  this->state_.max_collisions_known = true;
  this->state_.max_collisions = overview.max_collisions;

  this->state_.stalled_known = true;
  this->state_.stalled = overview.stalled;

  this->state_.lost_known = true;
  this->state_.lost = overview.lost;

  this->state_.limits_not_set_known = true;
  this->state_.limits_not_set = overview.limits_not_set;

  this->state_.xmr_alarm_known = true;
  this->state_.xmr_alarm = overview.xmr_alarm;

  ESP_LOGD(TAG, "Overview: layout=%s door=%s battery=%.1fV power=%s", protocol::layout_name(overview.layout),
           protocol::door_status_name(overview.door), this->state_.battery_voltage,
           protocol::power_status_name(overview.power));

  this->publish_state_();
}

void CenturionBLE::open() {
  if (!this->state_fresh_()) {
    ESP_LOGW(TAG, "Cannot open: no fresh status from the operator");
    return;
  }
  if (this->command_pending_ || this->queue_count_ > 0) {
    ESP_LOGW(TAG, "Cannot open: the previous command has not finished");
    return;
  }
  const uint8_t payload[] = {static_cast<uint8_t>(protocol::TRIGGER_ONLY_OPEN & 0xFF),
                             static_cast<uint8_t>((protocol::TRIGGER_ONLY_OPEN >> 8) & 0xFF)};
  this->queue_command_(protocol::CMD_TRIGGER, payload, sizeof(payload));
}

void CenturionBLE::close() {
  if (!this->state_fresh_()) {
    ESP_LOGW(TAG, "Cannot close: no fresh status from the operator");
    return;
  }
  if (this->command_pending_ || this->queue_count_ > 0) {
    ESP_LOGW(TAG, "Cannot close: the previous command has not finished");
    return;
  }
  const uint8_t payload[] = {static_cast<uint8_t>(protocol::TRIGGER_ONLY_CLOSE & 0xFF),
                             static_cast<uint8_t>((protocol::TRIGGER_ONLY_CLOSE >> 8) & 0xFF)};
  this->queue_command_(protocol::CMD_TRIGGER, payload, sizeof(payload));
}

void CenturionBLE::set_link_enabled(bool enabled) {
  if (enabled == this->link_enabled_)
    return;

  this->link_enabled_ = enabled;
  if (this->ble_parent_() == nullptr) {
    this->publish_state_();
    return;
  }

  if (enabled) {
    this->init_failures_ = 0;
    this->enter_idle_();
    this->ble_parent_()->set_enabled(true);
    // Do not connect() directly. Let ble_client's auto-connect act on the
    // next advertisement, so the key and address type are refreshed first.
  } else {
    this->reset_session_();
    this->ble_parent_()->set_enabled(false);
    this->ble_parent_()->disconnect();
    this->mark_disconnected_();
  }
  this->publish_state_();
}

void CenturionBLE::fail_init_(const char *reason) {
  this->init_failures_++;
  ESP_LOGW(TAG, "Session setup failed (%s), attempt %u of 3", reason,
           static_cast<unsigned>(this->init_failures_));

  if (this->init_failures_ >= 3) {
    ESP_LOGE(TAG,
             "Session setup failed three times; disabling the BLE link. "
             "Check the registered phone number, the advertisement key and the "
             "status-overview response.");
    this->link_enabled_ = false;
    if (this->ble_parent_() != nullptr)
      this->ble_parent_()->set_enabled(false);
    this->reset_session_();
    this->mark_disconnected_();
    this->publish_state_();
  } else {
    // Stop the state machine from sending the next queued init command while
    // the disconnect is in flight. The second loop() pass (the node is driven
    // by both App and BLEClient) must not see an active INIT state.
    this->session_state_ = SessionState::DISCONNECTING;
    this->queue_head_ = 0;
    this->queue_tail_ = 0;
    this->queue_count_ = 0;
    this->command_pending_ = false;
    this->disconnect_requested_ = true;
  }
}

void CenturionBLE::enter_idle_() {
  this->session_state_ = SessionState::IDLE;
  this->idle_started_ = millis();
}

void CenturionBLE::enter_wait_status_() {
  this->session_state_ = SessionState::WAIT_STATUS;
  this->status_started_ = millis();
  this->status_received_ = false;
  ESP_LOGD(TAG, "Authenticated; waiting for the first status overview");

  // An authenticated overview that arrived during the tail of the init
  // sequence is the first valid status for this session. It keeps its receipt
  // time: a slow init must not make old telemetry look fresh.
  if (this->has_pending_overview_) {
    const protocol::Overview overview = this->pending_overview_;
    const uint32_t received_ms = this->pending_overview_received_ms_;
    this->has_pending_overview_ = false;

    if (this->status_timeout_ > 0 &&
        millis() - received_ms >= this->status_timeout_) {
      ESP_LOGW(TAG,
               "Discarding status overview cached before init completed "
               "(%u ms old, timeout %u ms); waiting for a fresh one",
               static_cast<unsigned>(millis() - received_ms),
               static_cast<unsigned>(this->status_timeout_));
    } else {
      this->apply_overview_(overview, received_ms);
    }
  }
}

void CenturionBLE::begin_reconnect_(const char *reason) {
  ESP_LOGW(TAG, "Reconnecting: %s", reason);
  this->queue_head_ = 0;
  this->queue_tail_ = 0;
  this->queue_count_ = 0;
  this->command_pending_ = false;
  this->expire_state_();
  this->session_state_ = SessionState::DISCONNECTING;
  this->disconnect_requested_ = true;
}

void CenturionBLE::reset_session_() {
  this->enter_idle_();
  this->queue_head_ = 0;
  this->queue_tail_ = 0;
  this->queue_count_ = 0;
  this->command_pending_ = false;
  this->status_received_ = false;
  this->authenticated_ = false;
  this->has_pending_overview_ = false;
  this->pending_overview_received_ms_ = 0;
  this->disconnect_requested_ = false;
  this->write_handle_ = 0;
  this->notify_handle_ = 0;
}

void CenturionBLE::expire_state_() {
  const bool had_state = this->state_.connected || this->state_.door_known;
  // Do not leave last-known telemetry looking current once it is no longer
  // trustworthy. The entities treat the cleared *_known flags as unknown.
  this->state_ = CenturionState{};
  this->status_received_ = false;
  if (had_state)
    this->publish_state_();
}

void CenturionBLE::mark_disconnected_() { this->expire_state_(); }

bool CenturionBLE::state_fresh_() const {
  if (!this->state_.connected || this->session_state_ != SessionState::READY)
    return false;
  // Freshness is opt-in: the operator's URC cadence is not verified, so the
  // default (0) treats the last authenticated overview as usable.
  if (this->status_timeout_ == 0)
    return true;
  return (millis() - this->last_status_ms_) < this->status_timeout_;
}

void CenturionBLE::publish_state_() {
  this->state_callback_.call(this->state_);
}

}  // namespace esphome::centurion_ble

#endif  // USE_ESP32
