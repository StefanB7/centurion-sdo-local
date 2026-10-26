"""Centurion SMART BLE protocol helpers.

This module is deliberately free of Home Assistant and Bluetooth imports so it
can be unit-tested on its own. The wire format comes from the static analysis in
``docs/PROTOCOL.md``.

The layout has been confirmed against a real operator.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Mapping

SERVICE_UUID = "57b53956-da15-4005-8a1f-fcacde09af51"
WRITE_UUID = "57b5f92c-da15-4005-8a1f-fcacde09af51"
NOTIFY_UUID = "57b5db3b-da15-4005-8a1f-fcacde09af51"

CMD_AUTH_MOBILE_PHONE_A = 0x08000004
CMD_ENTER_STATUS_OVERVIEW = 0x08000022
CMD_EXIT_STATUS_OVERVIEW = 0x08000023
CMD_DISABLE_URC_BLOCK = 0x0800002D
CMD_ENABLE_URC_BLOCK = 0x0800002C
CMD_TRIGGER = 0x08000058
CMD_STOP_DOOR_MOTION = 0x08000067
CMD_CHANGE_BLE_CONNECTION_PARAMETERS = 0x0FFFFFFC

# Default trigger indexes from the SDO4/SDO5 configuration database. The app
# maps "Only Open" and "Only Close" to these indexes.
TRIGGER_TOGGLE = 1
TRIGGER_ONLY_OPEN = 5
TRIGGER_ONLY_CLOSE = 6

STATUS = {
    0: "complete",
    1: "read_reply",
    2: "ack",
    3: "nack",
    4: "timeout",
}

DOOR = {
    1: "open",
    2: "opening",
    3: "closed",
    4: "closing",
    5: "partially_open",
    6: "learn",
    7: "lost",
    255: "unknown",
}

POWER = {
    0: "normal",
    1: "low",
    2: "unknown",
    3: "mains_off",
    255: "invalid",
}

PRODUCTS = {
    13: "SDO4 SMART",
    39: "SDO5 SMART+",
}

# Buffer states used by the status overview URCs.
BUFFER_STATE_STATUS = 140
BUFFER_STATE_STATUS_EXTENDED = 164


@dataclass(frozen=True, slots=True)
class Advertisement:
    """The manufacturer-data fields the app uses to identify an operator."""

    serial_number_1: int
    serial_number_2: int
    key: bytes
    connect_state: int
    firmware_major: int
    firmware_minor: int
    product_code: int

    @property
    def model(self) -> str:
        return PRODUCTS.get(self.product_code, f"Unknown product {self.product_code}")

    @property
    def firmware(self) -> str:
        return f"{self.firmware_major}.{self.firmware_minor}"


@dataclass(frozen=True, slots=True)
class Notification:
    """A decoded ordinary reply or URC."""

    status: int
    payload: bytes
    buffer_state: int | None = None
    buffer_metadata: int | None = None
    overview: dict | None = None

    @property
    def status_name(self) -> str:
        return STATUS.get(self.status, "unknown")


def parse_advertisement(data: bytes) -> Advertisement:
    """Parse the app's 18- or 20-byte manufacturer data.

    ``data`` must be the manufacturer-data payload *without* the two-byte
    company identifier, matching Shiny's ``ManufacturerData.Data``.
    """

    if len(data) not in (18, 20):
        raise ValueError(
            f"Expected 18 or 20 bytes of manufacturer data, got {len(data)}"
        )

    key_offset = 10 if len(data) == 18 else 12
    return Advertisement(
        serial_number_1=struct.unpack_from("<i", data, 1)[0],
        serial_number_2=struct.unpack_from("<i", data, 5)[0],
        key=bytes(data[key_offset : key_offset + 4]),
        connect_state=data[key_offset + 4],
        firmware_major=data[key_offset + 5],
        firmware_minor=data[key_offset + 6],
        product_code=data[key_offset + 7],
    )


def find_advertisement(manufacturer_data: Mapping[int, bytes]) -> Advertisement | None:
    """Find the Centurion advertisement in HA's ``{company_id: data}`` map."""

    for data in manufacturer_data.values():
        if len(data) not in (18, 20):
            continue
        try:
            advertisement = parse_advertisement(bytes(data))
        except ValueError:
            continue
        if advertisement.product_code in PRODUCTS:
            return advertisement
    return None


def command(address: int, payload: bytes = b"") -> bytes:
    """Build a normal outgoing command frame (transfer type 2)."""

    if not 0 <= address <= 0xFFFFFFFF:
        raise ValueError("Command address must fit in 32 bits")
    return struct.pack(">BIIB", 2, address, len(payload), 0) + payload


def parse_key(value: str | None) -> bytes | None:
    """Parse a four-byte key from hex, or return None when empty."""

    if not value:
        return None
    try:
        key = bytes.fromhex(value.replace(":", "").replace("-", "").replace(" ", ""))
    except ValueError as err:
        raise ValueError("Key must be hexadecimal") from err
    if len(key) != 4:
        raise ValueError("Key must be exactly four bytes")
    return key


def encode_phone(phone: str, key: bytes) -> bytes:
    """Reproduce the app's XOR + nibble-swap phone obfuscation.

    ``phone`` must be the exact string the app stores for the registered
    international number. The key is the four bytes from the advertisement.
    """

    if len(key) != 4:
        raise ValueError("Authentication key must be exactly four bytes")

    encoded = bytearray()
    for index, value in enumerate(phone.encode("utf-8")):
        value ^= key[index % 4]
        encoded.append(((value << 4) | (value >> 4)) & 0xFF)
    return bytes(encoded)


def decode_overview(data: bytes, layout: str) -> dict:
    """Decode a packed status-overview body."""

    sizes = {"sdo4-v2": 20, "sdo5-v3": 24, "sdo5-v4": 44}
    expected = sizes.get(layout)
    if expected is None:
        raise ValueError(f"Unknown overview layout {layout!r}")
    if len(data) != expected:
        raise ValueError(f"{layout} requires {expected} bytes, got {len(data)}")

    flags, battery = struct.unpack_from("<IH", data)
    if layout == "sdo4-v2":
        seconds = data[7]
        timer = data[8]
        door_offset = 9
        feedback_offset = 16
    else:
        seconds = struct.unpack_from("<H", data, 8)[0]
        timer = data[10]
        door_offset = 12
        feedback_offset = 20

    feedback_index, trigger_index = struct.unpack_from("<HH", data, feedback_offset)
    result = {
        "layout": layout,
        "notification_flags_1": flags,
        "battery_voltage_raw": battery,
        "battery_voltage_v": battery / 10.0,
        "condition_flags": data[6],
        "seconds_remaining": seconds,
        "timer_flags": timer,
        "timer_active": bool(timer & 1),
        "door_status_raw": data[door_offset],
        "door_status": DOOR.get(data[door_offset], "unrecognised"),
        "beam_status_raw": data[door_offset + 1],
        "xmr_status_raw": data[door_offset + 2],
        "power_status_raw": data[door_offset + 3],
        "power_status": POWER.get(data[door_offset + 3], "unrecognised"),
        "feedback_handle": data[door_offset + 4],
        "trigger_handle": data[door_offset + 5],
        "feedback_index": feedback_index,
        "trigger_index": trigger_index,
        "collision": bool(flags & 0x00000001),
        "max_collisions": bool(flags & 0x00000020),
        "stalled": bool(flags & 0x00000080),
        "battery_low": bool(flags & 0x00000200),
        "lost": bool(flags & 0x00000400),
        "limits_not_set": bool(flags & 0x00000800),
        "xmr_alarm": bool(flags & 0x00004000),
        "mains_on": not bool(flags & 0x00010000),
    }
    if layout == "sdo5-v4":
        result["notification_flags_2"] = struct.unpack_from("<I", data, 24)[0]
        result["future_hex"] = data[28:].hex()
    return result


def decode_overview_auto(body: bytes, buffer_state: int) -> tuple[dict, str]:
    """Pick the overview layout from the URC buffer state and body length."""

    if buffer_state == BUFFER_STATE_STATUS_EXTENDED and len(body) == 44:
        layout = "sdo5-v4"
    elif buffer_state == BUFFER_STATE_STATUS and len(body) == 20:
        layout = "sdo4-v2"
    elif buffer_state == BUFFER_STATE_STATUS and len(body) == 24:
        layout = "sdo5-v3"
    else:
        raise ValueError(
            f"Unsupported overview URC (buffer state {buffer_state}, "
            f"{len(body)} body bytes)"
        )
    return decode_overview(body, layout), layout


def decode_notification(packet: bytes) -> Notification:
    """Decode an ordinary reply or status URC.

    File-transfer chunks use a different framing and are not handled here.
    """

    if len(packet) < 2 or len(packet) != packet[1] + 2:
        raise ValueError("Notification length does not match its two-byte header")

    status = packet[0]
    payload = packet[2:]
    if status != 0 or len(packet) < 4:
        return Notification(status=status, payload=payload)

    buffer_state = packet[2]
    buffer_metadata = packet[3]
    overview = None
    if buffer_state in (BUFFER_STATE_STATUS, BUFFER_STATE_STATUS_EXTENDED):
        overview, _ = decode_overview_auto(packet[4:], buffer_state)

    return Notification(
        status=status,
        payload=payload,
        buffer_state=buffer_state,
        buffer_metadata=buffer_metadata,
        overview=overview,
    )
