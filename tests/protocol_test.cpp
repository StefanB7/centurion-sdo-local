// Host-side checks for the pure C++ protocol codec.
//
//   g++ -std=c++17 -I tests tests/protocol_test.cpp components/centurion_ble/protocol.cpp -o /tmp/centurion_protocol_test
//   /tmp/centurion_protocol_test
//
// Vectors are generated from the Python reference by
// tools/generate_vectors.py.

#include "vectors.h"

#include "../components/centurion_ble/protocol.h"

#include <cstdio>
#include <cstring>

using namespace esphome::centurion_ble::protocol;

static int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
      failures++;                                                         \
    }                                                                     \
  } while (0)

static bool bytes_equal(const unsigned char *frame, size_t len, const unsigned char *expected,
                        size_t expected_len) {
  return len == expected_len && std::memcmp(frame, expected, len) == 0;
}

int main() {
  // --- Advertisement parsing ------------------------------------------------
  Advertisement adv;
  CHECK(parse_advertisement(vectors::ADV_18, sizeof(vectors::ADV_18), adv));
  CHECK(adv.key[0] == 0x11 && adv.key[1] == 0x22 && adv.key[2] == 0x33 && adv.key[3] == 0x44);
  CHECK(adv.product_code == 0x27);
  CHECK(adv.firmware_major == 1);
  CHECK(adv.firmware_minor == 2);
  CHECK(adv.connect_state == 0xFF);
  CHECK(adv.serial_number_1 == 0x12345678);
  CHECK(adv.serial_number_2 == 0x00ABCDEF);

  Advertisement adv20;
  CHECK(parse_advertisement(vectors::ADV_20, sizeof(vectors::ADV_20), adv20));
  CHECK(adv20.key[0] == 0x11 && adv20.key[3] == 0x44);
  CHECK(adv20.product_code == 0x27);

  CHECK(!parse_advertisement(nullptr, 0, adv));
  CHECK(!parse_advertisement(vectors::ADV_18, 17, adv));
  CHECK(is_supported_product(13));
  CHECK(is_supported_product(39));
  CHECK(!is_supported_product(1));

  // --- Command framing ------------------------------------------------------
  unsigned char frame[64];
  size_t len = build_command(CMD_AUTH_MOBILE_PHONE_A, reinterpret_cast<const uint8_t *>("+27821"), 6,
                             frame, sizeof(frame));
  CHECK(bytes_equal(frame, len, vectors::CMD_AUTH, sizeof(vectors::CMD_AUTH)));

  const uint8_t params[] = {0x00, 0x0A, 0x00, 0x0C, 0x00, 0x00, 0x0F, 0xA0};
  len = build_command(CMD_CHANGE_BLE_CONNECTION_PARAMETERS, params, sizeof(params), frame,
                      sizeof(frame));
  CHECK(bytes_equal(frame, len, vectors::CMD_CHANGE_PARAMS, sizeof(vectors::CMD_CHANGE_PARAMS)));

  const uint8_t open_payload[] = {0x05, 0x00};
  len = build_command(CMD_TRIGGER, open_payload, sizeof(open_payload), frame, sizeof(frame));
  CHECK(bytes_equal(frame, len, vectors::CMD_TRIGGER_OPEN, sizeof(vectors::CMD_TRIGGER_OPEN)));

  len = build_command(CMD_STOP_DOOR_MOTION, nullptr, 0, frame, sizeof(frame));
  CHECK(bytes_equal(frame, len, vectors::CMD_STOP, sizeof(vectors::CMD_STOP)));

  CHECK(build_command(CMD_TRIGGER, open_payload, sizeof(open_payload), frame, 5) == 0);

  // --- Phone obfuscation ----------------------------------------------------
  unsigned char phone[32];
  const uint8_t key[4] = {0x01, 0x02, 0x03, 0x04};
  size_t phone_len = encode_phone("+27821", key, phone, sizeof(phone));
  CHECK(bytes_equal(phone, phone_len, vectors::PHONE, sizeof(vectors::PHONE)));
  CHECK(encode_phone("", key, phone, sizeof(phone)) == 0);

  // --- Notifications --------------------------------------------------------
  Notification complete = decode_notification(reinterpret_cast<const uint8_t *>("\x00\x00"), 2);
  CHECK(complete.kind == NotificationKind::COMPLETE);

  Notification nack = decode_notification(reinterpret_cast<const uint8_t *>("\x03\x00"), 2);
  CHECK(nack.kind == NotificationKind::NACK);

  Notification timeout = decode_notification(reinterpret_cast<const uint8_t *>("\x04\x00"), 2);
  CHECK(timeout.kind == NotificationKind::TIMEOUT);

  Notification malformed = decode_notification(reinterpret_cast<const uint8_t *>("\x00"), 1);
  CHECK(malformed.kind == NotificationKind::INVALID);

  // Regression: a three-byte status-zero packet used to read past the end.
  const uint8_t short_urc[] = {0x00, 0x01, 0x8C};
  Notification short_notification = decode_notification(short_urc, sizeof(short_urc));
  CHECK(short_notification.kind == NotificationKind::OTHER);
  CHECK(!short_notification.has_overview);

  Notification n5 = decode_notification(vectors::SDO5_V3, sizeof(vectors::SDO5_V3));
  CHECK(n5.kind == NotificationKind::URC);
  CHECK(n5.has_overview);
  CHECK(n5.overview.layout == Layout::SDO5_V3);
  CHECK(n5.overview.door == DoorStatus::CLOSING);
  CHECK(n5.overview.battery_raw == 250);
  CHECK(n5.overview.seconds_remaining == 300);
  CHECK(n5.overview.feedback_index == 0x1234);
  CHECK(n5.overview.battery_low);
  CHECK(!n5.overview.mains_on);

  Notification n4 = decode_notification(vectors::SDO4_V2, sizeof(vectors::SDO4_V2));
  CHECK(n4.kind == NotificationKind::URC);
  CHECK(n4.has_overview);
  CHECK(n4.overview.layout == Layout::SDO4_V2);
  CHECK(n4.overview.door == DoorStatus::CLOSED);
  CHECK(n4.overview.battery_raw == 125);
  CHECK(n4.overview.seconds_remaining == 9);

  Notification n4x = decode_notification(vectors::SDO5_V4, sizeof(vectors::SDO5_V4));
  CHECK(n4x.kind == NotificationKind::URC);
  CHECK(n4x.has_overview);
  CHECK(n4x.overview.layout == Layout::SDO5_V4);
  CHECK(n4x.overview.notification_flags_2 == 0x12345678);

  // --- Names ----------------------------------------------------------------
  CHECK(std::strcmp(door_status_name(DoorStatus::PARTIALLY_OPEN), "partially_open") == 0);
  CHECK(std::strcmp(power_status_name(PowerStatus::MAINS_OFF), "mains_off") == 0);
  CHECK(std::strcmp(product_name(39), "SDO5 SMART+") == 0);
  CHECK(std::strcmp(layout_name(Layout::SDO5_V4), "sdo5-v4") == 0);

  if (failures == 0) {
    std::printf("all protocol vectors passed\n");
    return 0;
  }
  std::printf("%d check(s) failed\n", failures);
  return 1;
}
