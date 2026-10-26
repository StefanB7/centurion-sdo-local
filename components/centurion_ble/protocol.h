#pragma once

// Centurion SMART BLE wire protocol.
//
// This file is deliberately free of ESPHome/ESP-IDF dependencies so it can be
// compiled and tested on a normal host. It is a C++ port of
// tools/reference/protocol.py, which remains the
// reference implementation and the source of the test vectors.
//
// The layout comes from static analysis of MyCentsys Pro 1.5.0.218 and has
// been confirmed against a real operator.

#include <cstddef>
#include <cstdint>

namespace esphome::centurion_ble::protocol {

constexpr const char *SERVICE_UUID = "57b53956-da15-4005-8a1f-fcacde09af51";
constexpr const char *WRITE_UUID = "57b5f92c-da15-4005-8a1f-fcacde09af51";
constexpr const char *NOTIFY_UUID = "57b5db3b-da15-4005-8a1f-fcacde09af51";

constexpr uint32_t CMD_AUTH_MOBILE_PHONE_A = 0x08000004;
constexpr uint32_t CMD_ENTER_STATUS_OVERVIEW = 0x08000022;
constexpr uint32_t CMD_EXIT_STATUS_OVERVIEW = 0x08000023;
constexpr uint32_t CMD_DISABLE_URC_BLOCK = 0x0800002D;
constexpr uint32_t CMD_ENABLE_URC_BLOCK = 0x0800002C;
constexpr uint32_t CMD_TRIGGER = 0x08000058;
constexpr uint32_t CMD_STOP_DOOR_MOTION = 0x08000067;
constexpr uint32_t CMD_CHANGE_BLE_CONNECTION_PARAMETERS = 0x0FFFFFFC;

// Default trigger indexes from the SDO4/SDO5 configuration database.
constexpr uint16_t TRIGGER_TOGGLE = 1;
constexpr uint16_t TRIGGER_ONLY_OPEN = 5;
constexpr uint16_t TRIGGER_ONLY_CLOSE = 6;

constexpr uint8_t BUFFER_STATE_STATUS = 140;
constexpr uint8_t BUFFER_STATE_STATUS_EXTENDED = 164;

enum class Layout : uint8_t {
  UNKNOWN = 0,
  SDO4_V2,
  SDO5_V3,
  SDO5_V4,
};

// Raw values are kept equal to the controller's enum so a cast is safe.
// UNRECOGNISED is the "no data yet / unknown raw value" sentinel.
enum class DoorStatus : uint8_t {
  UNRECOGNISED = 0,
  OPEN = 1,
  OPENING = 2,
  CLOSED = 3,
  CLOSING = 4,
  PARTIALLY_OPEN = 5,
  LEARN = 6,
  LOST = 7,
  UNKNOWN = 255,
};

// UNRECOGNISED is 254 because 255 is itself a valid raw value (INVALID).
enum class PowerStatus : uint8_t {
  NORMAL = 0,
  LOW = 1,
  UNKNOWN = 2,
  MAINS_OFF = 3,
  UNRECOGNISED = 254,
  INVALID = 255,
};

struct Advertisement {
  int32_t serial_number_1{0};
  int32_t serial_number_2{0};
  uint8_t key[4]{0, 0, 0, 0};
  uint8_t connect_state{0};
  uint8_t firmware_major{0};
  uint8_t firmware_minor{0};
  uint8_t product_code{0};
};

struct Overview {
  Layout layout{Layout::UNKNOWN};
  uint32_t notification_flags_1{0};
  uint32_t notification_flags_2{0};
  uint16_t battery_raw{0};  // tenths of a volt
  uint8_t condition_flags{0};
  uint16_t seconds_remaining{0};
  uint8_t timer_flags{0};
  bool timer_active{false};
  DoorStatus door{DoorStatus::UNRECOGNISED};
  uint8_t door_raw{0};
  uint8_t beam_raw{0};
  uint8_t xmr_raw{0};
  PowerStatus power{PowerStatus::UNRECOGNISED};
  uint8_t power_raw{0};
  uint8_t feedback_handle{0};
  uint8_t trigger_handle{0};
  uint16_t feedback_index{0};
  uint16_t trigger_index{0};
  bool collision{false};
  bool max_collisions{false};
  bool stalled{false};
  bool battery_low{false};
  bool lost{false};
  bool limits_not_set{false};
  bool xmr_alarm{false};
  bool mains_on{true};
};

enum class NotificationKind : uint8_t {
  INVALID = 0,
  COMPLETE,
  READ_REPLY,
  ACK,
  NACK,
  TIMEOUT,
  URC,
  OTHER,
};

struct Notification {
  NotificationKind kind{NotificationKind::INVALID};
  uint8_t status{0};
  uint8_t buffer_state{0};
  uint8_t buffer_metadata{0};
  bool has_overview{false};
  Overview overview{};
};

// Parse the app's 18- or 20-byte manufacturer-data payload. `data` must not
// include the two-byte company identifier (ESPHome and Home Assistant both
// strip it already).
bool parse_advertisement(const uint8_t *data, size_t len, Advertisement &out);

bool is_supported_product(uint8_t product_code);
const char *product_name(uint8_t product_code);

// Decode a status-overview body. `buffer_state` is the URC byte at offset 2.
bool decode_overview(const uint8_t *body, size_t len, uint8_t buffer_state, Overview &out);

// Decode any ordinary reply or URC.
Notification decode_notification(const uint8_t *data, size_t len);

// Build the 10-byte header + payload. Returns the frame length, or 0 on error.
size_t build_command(uint32_t address, const uint8_t *payload, size_t payload_len, uint8_t *out,
                     size_t out_size);

// Reproduce the app's XOR + nibble-swap phone obfuscation.
// Returns the encoded length, or 0 on error.
size_t encode_phone(const char *phone, const uint8_t key[4], uint8_t *out, size_t out_size);

const char *door_status_name(DoorStatus status);
const char *power_status_name(PowerStatus status);
const char *layout_name(Layout layout);

}  // namespace esphome::centurion_ble::protocol
