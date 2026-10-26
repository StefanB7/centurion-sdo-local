#include "protocol.h"

#include <cstring>

namespace esphome::centurion_ble::protocol {

namespace {

inline uint16_t read_le16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t read_le32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline DoorStatus decode_door(uint8_t raw) {
  switch (raw) {
    case 1:
      return DoorStatus::OPEN;
    case 2:
      return DoorStatus::OPENING;
    case 3:
      return DoorStatus::CLOSED;
    case 4:
      return DoorStatus::CLOSING;
    case 5:
      return DoorStatus::PARTIALLY_OPEN;
    case 6:
      return DoorStatus::LEARN;
    case 7:
      return DoorStatus::LOST;
    case 255:
      return DoorStatus::UNKNOWN;
    default:
      return DoorStatus::UNRECOGNISED;
  }
}

inline PowerStatus decode_power(uint8_t raw) {
  switch (raw) {
    case 0:
      return PowerStatus::NORMAL;
    case 1:
      return PowerStatus::LOW;
    case 2:
      return PowerStatus::UNKNOWN;
    case 3:
      return PowerStatus::MAINS_OFF;
    case 255:
      return PowerStatus::INVALID;
    default:
      return PowerStatus::UNRECOGNISED;
  }
}

}  // namespace

bool parse_advertisement(const uint8_t *data, size_t len, Advertisement &out) {
  if (data == nullptr || (len != 18 && len != 20))
    return false;

  const size_t key_offset = (len == 18) ? 10 : 12;
  out.serial_number_1 = static_cast<int32_t>(read_le32(data + 1));
  out.serial_number_2 = static_cast<int32_t>(read_le32(data + 5));
  std::memcpy(out.key, data + key_offset, sizeof(out.key));
  out.connect_state = data[key_offset + 4];
  out.firmware_major = data[key_offset + 5];
  out.firmware_minor = data[key_offset + 6];
  out.product_code = data[key_offset + 7];
  return true;
}

bool is_supported_product(uint8_t product_code) {
  return product_code == 13 || product_code == 39;
}

const char *product_name(uint8_t product_code) {
  switch (product_code) {
    case 13:
      return "SDO4 SMART";
    case 39:
      return "SDO5 SMART+";
    default:
      return "SMART operator";
  }
}

bool decode_overview(const uint8_t *body, size_t len, uint8_t buffer_state, Overview &out) {
  if (body == nullptr)
    return false;

  Layout layout;
  size_t door_offset;
  size_t feedback_offset;
  uint16_t seconds;
  uint8_t timer;

  if (buffer_state == BUFFER_STATE_STATUS_EXTENDED && len == 44) {
    layout = Layout::SDO5_V4;
    door_offset = 12;
    feedback_offset = 20;
    seconds = read_le16(body + 8);
    timer = body[10];
  } else if (buffer_state == BUFFER_STATE_STATUS && len == 20) {
    layout = Layout::SDO4_V2;
    door_offset = 9;
    feedback_offset = 16;
    seconds = body[7];
    timer = body[8];
  } else if (buffer_state == BUFFER_STATE_STATUS && len == 24) {
    layout = Layout::SDO5_V3;
    door_offset = 12;
    feedback_offset = 20;
    seconds = read_le16(body + 8);
    timer = body[10];
  } else {
    return false;
  }

  Overview result{};
  result.layout = layout;
  result.notification_flags_1 = read_le32(body);
  result.battery_raw = read_le16(body + 4);
  result.condition_flags = body[6];
  result.seconds_remaining = seconds;
  result.timer_flags = timer;
  result.timer_active = (timer & 0x01) != 0;

  result.door_raw = body[door_offset];
  result.door = decode_door(result.door_raw);
  result.beam_raw = body[door_offset + 1];
  result.xmr_raw = body[door_offset + 2];
  result.power_raw = body[door_offset + 3];
  result.power = decode_power(result.power_raw);
  result.feedback_handle = body[door_offset + 4];
  result.trigger_handle = body[door_offset + 5];

  result.feedback_index = read_le16(body + feedback_offset);
  result.trigger_index = read_le16(body + feedback_offset + 2);

  const uint32_t flags = result.notification_flags_1;
  result.collision = (flags & 0x00000001u) != 0;
  result.max_collisions = (flags & 0x00000020u) != 0;
  result.stalled = (flags & 0x00000080u) != 0;
  result.battery_low = (flags & 0x00000200u) != 0;
  result.lost = (flags & 0x00000400u) != 0;
  result.limits_not_set = (flags & 0x00000800u) != 0;
  result.xmr_alarm = (flags & 0x00004000u) != 0;
  result.mains_on = (flags & 0x00010000u) == 0;

  if (layout == Layout::SDO5_V4)
    result.notification_flags_2 = read_le32(body + 24);

  out = result;
  return true;
}

Notification decode_notification(const uint8_t *data, size_t len) {
  Notification notification;
  if (data == nullptr || len < 2 || len != static_cast<size_t>(data[1]) + 2)
    return notification;

  notification.status = data[0];

  if (len == 2) {
    switch (data[0]) {
      case 0:
        notification.kind = NotificationKind::COMPLETE;
        break;
      case 1:
        notification.kind = NotificationKind::READ_REPLY;
        break;
      case 2:
        notification.kind = NotificationKind::ACK;
        break;
      case 3:
        notification.kind = NotificationKind::NACK;
        break;
      case 4:
        notification.kind = NotificationKind::TIMEOUT;
        break;
      default:
        notification.kind = NotificationKind::OTHER;
        break;
    }
    return notification;
  }

  if (data[0] != 0) {
    notification.kind = (data[0] == 1) ? NotificationKind::READ_REPLY : NotificationKind::OTHER;
    return notification;
  }

  // Status zero with fewer than four bytes cannot carry the URC header
  // (status, length, buffer state, buffer metadata). Treat it as a short
  // ordinary reply instead of reading past the end of the packet.
  if (len < 4) {
    notification.kind = NotificationKind::OTHER;
    return notification;
  }

  notification.buffer_state = data[2];
  notification.buffer_metadata = data[3];

  if (notification.buffer_state == BUFFER_STATE_STATUS ||
      notification.buffer_state == BUFFER_STATE_STATUS_EXTENDED) {
    notification.has_overview =
        decode_overview(data + 4, len - 4, notification.buffer_state, notification.overview);
    notification.kind = NotificationKind::URC;
  } else {
    notification.kind = NotificationKind::URC;
  }

  return notification;
}

size_t build_command(uint32_t address, const uint8_t *payload, size_t payload_len, uint8_t *out,
                     size_t out_size) {
  if (out == nullptr || payload_len > 0xFFFFFFFFu)
    return 0;

  const size_t total = 10 + payload_len;
  if (out_size < total)
    return 0;

  out[0] = 2;  // command with response
  out[1] = static_cast<uint8_t>((address >> 24) & 0xFF);
  out[2] = static_cast<uint8_t>((address >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((address >> 8) & 0xFF);
  out[4] = static_cast<uint8_t>(address & 0xFF);
  const uint32_t size = static_cast<uint32_t>(payload_len);
  out[5] = static_cast<uint8_t>((size >> 24) & 0xFF);
  out[6] = static_cast<uint8_t>((size >> 16) & 0xFF);
  out[7] = static_cast<uint8_t>((size >> 8) & 0xFF);
  out[8] = static_cast<uint8_t>(size & 0xFF);
  out[9] = 0;  // last-chunk flag

  if (payload_len > 0 && payload != nullptr)
    std::memcpy(out + 10, payload, payload_len);

  return total;
}

size_t encode_phone(const char *phone, const uint8_t key[4], uint8_t *out, size_t out_size) {
  if (phone == nullptr || key == nullptr || out == nullptr)
    return 0;

  const size_t len = std::strlen(phone);
  if (len == 0 || out_size < len)
    return 0;

  for (size_t i = 0; i < len; i++) {
    const uint8_t value = static_cast<uint8_t>(phone[i]) ^ key[i % 4];
    out[i] = static_cast<uint8_t>(((value << 4) | (value >> 4)) & 0xFF);
  }
  return len;
}

const char *door_status_name(DoorStatus status) {
  switch (status) {
    case DoorStatus::OPEN:
      return "open";
    case DoorStatus::OPENING:
      return "opening";
    case DoorStatus::CLOSED:
      return "closed";
    case DoorStatus::CLOSING:
      return "closing";
    case DoorStatus::PARTIALLY_OPEN:
      return "partially_open";
    case DoorStatus::LEARN:
      return "learn";
    case DoorStatus::LOST:
      return "lost";
    case DoorStatus::UNKNOWN:
      return "unknown";
    default:
      return "unrecognised";
  }
}

const char *power_status_name(PowerStatus status) {
  switch (status) {
    case PowerStatus::NORMAL:
      return "normal";
    case PowerStatus::LOW:
      return "low";
    case PowerStatus::UNKNOWN:
      return "unknown";
    case PowerStatus::MAINS_OFF:
      return "mains_off";
    case PowerStatus::INVALID:
      return "invalid";
    default:
      return "unrecognised";
  }
}

const char *layout_name(Layout layout) {
  switch (layout) {
    case Layout::SDO4_V2:
      return "sdo4-v2";
    case Layout::SDO5_V3:
      return "sdo5-v3";
    case Layout::SDO5_V4:
      return "sdo5-v4";
    default:
      return "unknown";
  }
}

}  // namespace esphome::centurion_ble::protocol
