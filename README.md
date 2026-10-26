# centurion-sdo-local

**Local-only control of Centurion SDO4 SMART and SDO5 SMART+ garage door operators from an ESP32.**
Bluetooth Low Energy in, MQTT over TLS out. No cloud, no vendor app, Home Assistant optional.

> [!WARNING]
> **Use at your own risk.** The protocol was reverse-engineered from the vendor app and confirmed working on an SDO5 SMART+, but other models or firmware versions may behave differently. This controls a physical garage door: keep the original remote at hand and the door in view when trying something new. This is not a safety device; the operator's own beams, limits and force settings remain the safety system.

> *Unofficial. Not affiliated with, endorsed by, or supported by Centurion Systems.*

## How it works

```
┌─────────────┐   BLE GATT    ┌───────────────────────────┐  MQTT/TLS  ┌─────────────┐
│  SDO4/SDO5  │◄─────────────►│ ESP32 running ESPHome     │───────────►│ MQTT broker │
│  operator   │  notify/write │ + centurion_ble component │   :8883    │ (HA optional)│
└─────────────┘               └───────────────────────────┘            └─────────────┘
```

The ESP32 acts as the BLE central. It authenticates with the operator, decodes status notifications and publishes the door as a standard MQTT device. Home Assistant picks it up through MQTT discovery, but any MQTT client works.

## Features

- **Open / close** as a cover entity
- **Live status:** door state, battery, timer, beam, mains and fault flags
- **MQTT over TLS** with optional mutual TLS and combined node and BLE availability (LWT)
- **No HTTP control path:** no `web_server`, no native API by default
- **BLE release switch:** hand the single BLE session back to the phone app, then take it again
- **Host-tested C++ codec** checked against a Python reference with golden vectors

## Quick start

Tested on an **SDO5 SMART+** only. SDO4 SMART support follows the same protocol analysis but is untested.

Requires [ESPHome](https://esphome.io) (tested with 2026.9.1), a classic ESP32 board, and an MQTT broker with TLS.

```sh
git clone https://github.com/StefanB7/centurion-sdo-local.git
cd centurion-sdo-local

cp secrets.example.yaml secrets.yaml   # fill in Wi-Fi, MQTT, CA cert, operator MAC, phone number
esphome run centurion.yaml
```

Then watch the door:

```sh
mosquitto_sub -h <broker> -p 8883 --cafile ca.pem -u <user> -P <pass> -t 'centurion-garage/#' -v
```

Full setup, MQTT topics, TLS and broker hardening, the bench test plan and troubleshooting are in the **[guide](docs/GUIDE.md)**.

> Never publish door commands with the MQTT retain flag.

## Repository layout

| Path | Purpose |
|---|---|
| `centurion.yaml` | ESPHome device configuration |
| `components/centurion_ble/` | External ESPHome component: BLE session, codec, entities |
| `tests/` | C++ host tests (codec, session, entities, MQTT availability) |
| `tools/` | Golden-vector generator and the Python reference codec |
| `docs/GUIDE.md` | Full setup and operations guide |
| `docs/PROTOCOL.md` | The Centurion BLE protocol as understood so far |

## Tests

```sh
bash tests/run_host_tests.sh
SANITIZE=1 bash tests/run_host_tests.sh   # with ASan + UBSan
```

These use synthetic data. Compatibility with your operator, TLS and broker behaviour still need a bench check on real hardware (see the guide).

## Limitations

- One BLE session per operator: the phone app and this firmware can't be connected at once.
- Commands are momentary triggers. There is no auto-close.
- The phone number and key are stored in your secrets and in the compiled firmware. The key is reversible obfuscation, not cryptography. Treat firmware images as sensitive.

## Contributing

Bench results, captures from real SDO4/SDO5 units and protocol corrections are especially welcome. Please open an issue.

## License

[MIT](LICENSE)
