# Centurion SDO4 / SDO5 — ESP32-only controller (MQTT over TLS)

A standalone ESP32 firmware for Centurion **SDO4 SMART** and **SDO5 SMART+**
garage door operators. The ESP32 is a BLE central: it speaks the Centurion
serial protocol itself and publishes the door over MQTT. **Home Assistant is
optional** — any MQTT client can drive it.

The wire format is documented in [PROTOCOL.md](PROTOCOL.md). Only one BLE
client can talk to an operator at a time (see [Link sharing](#link-sharing)).

> **Status: confirmed working on an SDO5 SMART+; SDO4 is untested.** The protocol was
> reverse-engineered from the vendor app. Other models or firmware versions may
> differ, so run the bench tests below on your setup and keep the physical
> remote and MyCentsys Pro available while testing.

## Architecture

```
┌─────────────┐   BLE GATT    ┌──────────────────────────────┐  MQTT/TLS  ┌────────────┐
│  SDO4/SDO5  │◄─────────────►│ ESP32 (ESPHome external       │───────────►│ MQTT broker│
│  operator   │  notify/write │ component: auth, URC decode,  │   :8883    │ (optional  │
└─────────────┘               │ cover/sensors/switch)         │            │  HA)       │
                              └──────────────────────────────┘            └────────────┘
```

- `ble_client` owns the GATT connection and negotiates MTU automatically.
- `centurion_ble` (the hub) implements the session: key capture, auth,
  status-overview URC decoding, command queue, reconnect handling.
- `cover`, `sensor`, `binary_sensor`, `text_sensor` and `switch` platforms are
  thin views over the hub's `CenturionState`.
- ESPHome's MQTT component publishes HA discovery automatically. The same
  topics work without HA.
- The default build is **pure MQTT**: no `web_server` and no native `api`.
  OTA remains enabled.

## Layout

```
centurion-sdo-local/
├── centurion.yaml                         # device config
├── secrets.example.yaml
├── components/centurion_ble/
│   ├── __init__.py                        # hub codegen
│   ├── centurion_ble.h / .cpp             # BLE session + protocol state machine
│   ├── protocol.h / .cpp                  # pure C++ codec (host-testable)
│   ├── mqtt_availability.h / .py          # node + BLE discovery availability
│   ├── cover/                             # cover platform
│   ├── sensor/                            # battery / timer / beam
│   ├── binary_sensor/                     # connected / mains / fault flags
│   ├── text_sensor/                       # door / power status strings
│   └── switch/                            # release the BLE link for the app
├── tools/generate_vectors.py              # Python reference -> tests/vectors.h
├── tools/reference/protocol.py            # Python reference codec
├── docs/                                  # this guide + protocol notes
└── tests/run_host_tests.sh                # codec, session, entity, MQTT tests
```

## What it exposes

| Entity | Type | MQTT state / command | Operator availability |
|---|---|---|---|
| Garage Door | `cover` | `.../cover/garage_door/state` (`open`/`closed`/`opening`/`closing`/`unknown`), command `OPEN`/`CLOSE` | yes |
| Battery voltage | `sensor` | volts | yes |
| Timer remaining | `sensor` | seconds | yes |
| Beam status raw | `sensor` | raw product-specific value | yes |
| Door status | `text_sensor` | `open`, `opening`, `closed`, `closing`, `partially_open`, `learn`, `lost`, `unknown` | yes |
| Power status | `text_sensor` | `normal`, `low`, `unknown`, `mains_off`, `invalid` | yes |
| Mains power | `binary_sensor` | mains present | yes |
| Battery low / collision / max collisions / stalled / lost / limits not set / XMR alarm | `binary_sensor` | fault flags | yes |
| BLE connected | `binary_sensor` | session authenticated and first valid overview decoded | node only |
| BLE connection | `switch` | turn off to release the link for MyCentsys Pro | node only |

Operator availability is deliberately stricter than node availability. The BLE
diagnostic and link switch use node availability only, so the switch remains
usable while the operator session is off; operator entities also require the
BLE session. See [MQTT availability](#mqtt-availability).

### Commands

The cover sends the **Only Open** and **Only Close** trigger indexes (5 and 6).
It does not send raw motor commands. There is no stop command. OPEN/CLOSE
require a fresh, authenticated status overview; they are refused otherwise.

## MQTT availability

Operator entities have **two** availability conditions in Home Assistant:

1. ESPHome's normal node topic, `${name}/status`, with the default birth
   `online` and LWT/shutdown `offline`.
2. The retained BLE session topic,
   `${name}/binary_sensor/ble_connected/state`, published as `ON` when the
   session is authenticated and has delivered a valid overview, and `OFF`
   otherwise.

The component's MQTT wrapper adds both topics to the Home Assistant discovery
message with `availability_mode: all`. An operator entity is therefore
available only when the ESP32 is connected to the broker **and** the BLE
session is usable.

The BLE diagnostic pins its state topic and retention in `centurion.yaml`:

```yaml
- platform: centurion_ble
  type: connected
  id: ble_connected
  state_topic: ${name}/binary_sensor/ble_connected/state
  retain: true
```

Every operator-dependent entity points at that retained `ON`/`OFF` state:

```yaml
availability:
  topic: centurion-garage/binary_sensor/ble_connected/state
  payload_available: "ON"
  payload_not_available: "OFF"
```

This design handles device power loss correctly:

- If the ESP32 loses power, the broker publishes the node LWT `offline` on
  `${name}/status`, regardless of the retained BLE state. With `mode: all`,
  Home Assistant marks every operator entity unavailable even if the last
  retained BLE message was `ON`. A stale retained `ON` cannot win.
- If the BLE session is lost or expired while the ESP32 stays
  online, the binary sensor publishes `OFF`, so operator entities become
  unavailable while the node remains online.
- On boot, the node birth publishes `online`; the retained BLE state supplies
  the other half. If the BLE state is missing or `OFF`, operator entities stay
  unavailable until a valid session publishes `ON`.
- On broker restart, the node birth and the retained BLE state together restore
  the correct result; `clean_session: true` does not remove retained messages.

The **BLE connected** diagnostic and the **BLE connection** switch have no
operator availability override. They use node-only availability, so the switch
remains visible and usable while the BLE session is off, which is how you take
the link back from MyCentsys Pro. If the ESP32 itself is offline, the node LWT
still marks them unavailable.

Direct MQTT clients that do not consume Home Assistant discovery must apply the
same AND themselves: `${name}/status` must be `online` **and**
`${name}/binary_sensor/ble_connected/state` must be `ON`. The two-topic list and
`availability_mode: all` are emitted by the component wrapper; this is not
plain ESPHome YAML behaviour. Without that wrapper, stock ESPHome would
advertise only the BLE topic and could leave a stale retained `ON` looking
available after the node crashed, so do not deploy this YAML against a component
build that lacks the wrapper.

## Prerequisites

- Centurion **SDO4 SMART** or **SDO5 SMART+**, already set up in MyCentsys Pro
  with a registered phone number.
- An **ESP32** (classic, S3 or C3) with Wi-Fi and BLE. Classic `esp32dev` is
  the default board.
- **ESPHome 2026.9.1**, with per-entity MQTT `availability`, `state_topic`, `retain`
  and `idf_send_async` support. Pin a version and re-run `esphome config` after
  upgrading. Availability *lists* with `availability_mode: all` are emitted by
  the operator platform wrapper in this repository, not by stock ESPHome alone.
- An MQTT broker with **TLS on port 8883**, a server certificate whose SAN
  matches `mqtt_broker`, and a CA certificate you can place in `secrets.yaml`.
- The operator within BLE range. Walls and the door attenuate BLE; place the
  ESP32 sensibly.

## Configure and flash

1. Copy `secrets.example.yaml` to `secrets.yaml` in this directory and fill in
   every value. `secrets.yaml` is git-ignored.

2. **MQTT TLS.** Set `mqtt_broker` to the DNS hostname in the broker
   certificate's SAN. Put the PEM CA certificate in `mqtt_ca_certificate`.
   `centurion.yaml` already sets `port: 8883`,
   `certificate_authority: !secret mqtt_ca_certificate`,
   `skip_cert_cn_check: false`, `clean_session: true`,
   `idf_send_async: true` and `reboot_timeout: 0s`.

   ESP-IDF/mbedTLS does not validate wildcard certificates. Use an exact
   hostname such as `mqtt.example.com`, not `*.example.com`. An IP address works
   only if the certificate contains that IP in its SAN.

3. Guarantee the broker hostname resolves on the device's network. A hostname
   that resolves to the broker but is not in the certificate SAN fails the
   handshake, by design.

4. Find the operator's **Bluetooth MAC address**. Flash the firmware once with
   a placeholder MAC, open the ESPHome logs, and look for the tracker's
   discovered-device lines; or use any BLE scanner app. Put it in
   `secrets.yaml` as `centurion_mac`.

5. Put the **exact international phone number** registered in MyCentsys Pro in
   `centurion_phone` (e.g. `"+27821234567"`). Keep the quotes: YAML would
   otherwise parse the leading `+` as an integer and drop it. Spaces or
   punctuation the app does not store will change the encoded bytes.

6. The four-byte advertisement **key is optional**. Leave `key:` commented out
   and the component captures it from the operator's advertisement on every
   connect. If you set it, the advertisement value still wins when it is seen,
   so a rotating key keeps working.

7. Adjust `board:` in `centurion.yaml` if needed, then:

   ```sh
   esphome config centurion.yaml
   esphome run centurion.yaml
   ```

   `esphome run` flashes over USB or OTA. OTA is preserved. There is no
   `web_server`; the fallback AP runs only the captive-portal provisioning page,
   not entity control.

Optional session tuning, shown commented in `centurion.yaml`:

```yaml
centurion_ble:
  # initial_status_timeout: 15s  # default; 0s disables
  # status_timeout: 0s           # default disabled
```

- `initial_status_timeout` (default `15s`) is the maximum wait for the first
  valid status overview after authentication. On expiry the session reconnects;
  three failed session setups disable the BLE link.
- `status_timeout` (default `0s`, disabled) makes an overview older than the
  given age stale. Enabling it invalidates entities and forces a reconnect
  before further commands. The operator's URC cadence is **not verified**, so
  do not assume a heartbeat. Leave it at `0s` until you have measured the real
  cadence and accept reconnect churn.

## MQTT TLS and broker security

No broker deployment or credentials are included. A safe outline:

- Use a **dedicated MQTT account** for this device, never an administrator
  account. Allow reads of this device's command topics and writes of its state,
  log and status topics under `centurion-garage/#` (adapt to your naming).
  Home Assistant discovery also needs writes to
  `homeassistant/+/centurion-garage/+/config`. ESPHome's MQTT discovery uses
  writes to `esphome/discover/centurion-garage` and reads of
  `esphome/discover` and `esphome/ping/centurion-garage` (or disable
  `discover_ip`). Do not grant access to other devices' command topics.
- Keep anonymous access disabled and require TLS. The device enforces the
  broker certificate and hostname; `skip_cert_cn_check: false` is intentional.
- **Certificate dates:** the ESPHome 2026.9.1 default build disables
  `CONFIG_MBEDTLS_HAVE_TIME_DATE`, so CA/hostname verification alone does not
  enforce certificate expiry. To enforce validity dates, merge the following
  into the existing `esp32.framework` block and add an SNTP time source. MQTT
  may fail initially and retry until time synchronises. Use a reachable local
  NTP server instead of the public example for a fully local deployment:

  ```yaml
  esp32:
    board: esp32dev
    framework:
      type: esp-idf
      sdkconfig_options:
        CONFIG_MBEDTLS_HAVE_TIME_DATE: "y"

  time:
    - platform: sntp
      timezone: UTC0
      servers: [0.pool.ntp.org]
  ```

  This requires correct time after every cold boot; SNTP itself is not
  authenticated. Include clock availability in your bench tests.
- Optional **mutual TLS**: if the broker requires a client certificate, add
  `mqtt_client_certificate` and `mqtt_client_key` to `secrets.yaml`, then
  uncomment `client_certificate` / `client_certificate_key` in `centurion.yaml`.
  Keep the CA configured. Never commit the private key.
- **Never publish motion commands with `retain: true`.** `clean_session: true`
  tells the broker to discard the client session on disconnect; it does **not**
  remove retained messages. A retained OPEN/CLOSE is delivered again on the
  next subscribe/reconnect and can move the door unexpectedly.
- Clear stale retained commands after experiments:

  ```sh
  # -r -n publishes a zero-length retained payload, deleting the retained value
  mosquitto_pub -h mqtt.example.com -p 8883 --cafile ca.pem \
    -u DEVICE_USER -P DEVICE_PASSWORD \
    -r -n -t 'centurion-garage/cover/garage_door/command'
  ```

  Repeat for `.../switch/ble_connection/command` if it was ever retained. Better:
  use a broker account for the device that cannot publish commands, and keep
  command publishing on a separate trusted account.
- The device subscribes to its command topics only after connecting. With
  `clean_session: true` there is no offline command queue, but retained
  messages are still replayed.

## MQTT without Home Assistant

ESPHome's default topic prefix is the device name, so with `name:
centurion-garage`:

```sh
# Watch state and availability (TLS shown explicitly)
mosquitto_sub -h mqtt.example.com -p 8883 --cafile ca.pem \
  -u DEVICE_USER -P DEVICE_PASSWORD -v -t 'centurion-garage/#'

# Open or close — never use -r/--retain for these
mosquitto_pub -h mqtt.example.com -p 8883 --cafile ca.pem \
  -u DEVICE_USER -P DEVICE_PASSWORD \
  -t 'centurion-garage/cover/garage_door/command' -m OPEN
mosquitto_pub -h mqtt.example.com -p 8883 --cafile ca.pem \
  -u DEVICE_USER -P DEVICE_PASSWORD \
  -t 'centurion-garage/cover/garage_door/command' -m CLOSE

# Release the BLE link for the phone app, then take it back
mosquitto_pub -h mqtt.example.com -p 8883 --cafile ca.pem \
  -u DEVICE_USER -P DEVICE_PASSWORD \
  -t 'centurion-garage/switch/ble_connection/command' -m OFF
mosquitto_pub -h mqtt.example.com -p 8883 --cafile ca.pem \
  -u DEVICE_USER -P DEVICE_PASSWORD \
  -t 'centurion-garage/switch/ble_connection/command' -m ON
```

Watch both halves of the availability decision:

```sh
# Node availability: online/offline (retained, with LWT)
... -t 'centurion-garage/status'

# BLE session: ON/OFF (retained)
... -t 'centurion-garage/binary_sensor/ble_connected/state'
```

With `discovery: true`, Home Assistant (if present) creates the entities
automatically and marks operator entities unavailable while either MQTT or the
BLE session is down. A client that does not read discovery must test both topics
itself.

## Optional native API

The default build removes `api:` to save RAM for the BLE session and TLS. If
you want the encrypted native API as well, add:

```yaml
api:
  reboot_timeout: 0s
  encryption:
    key: !secret api_encryption_key
```

Then set `api_encryption_key` in `secrets.yaml` (generate one with
`openssl rand -base64 32`). `reboot_timeout: 0s` is required for a device that
may have no API client, otherwise ESPHome reboots after the default timeout.
The API is independent of OTA. Measure RAM after enabling it; do not assume the
pure-MQTT headroom is enough for TLS, BLE and the API together.

## How the session works

On each connection:

1. The advertisement listener captures the four-byte key, product code and
   firmware (the company ID is already stripped by ESPHome).
2. `ble_client` connects and negotiates MTU; the component discovers the serial
   service and subscribes to notifications.
3. After the app's 1.5 s settle time, it sends:
   `ChangeBLEConnectionParameters` → `AuthMobilePhoneA` → `DisableURCBlock` →
   `EnterStatusOverviewMode`.
4. Each init command waits for `00 00`; `03 00`/`04 00` are NACK/timeout. After
   three failed session setups the link is disabled so a wrong phone number or
   key does not loop forever.
5. The first valid status overview (buffer state `0x8C` or `0xA4`) is decoded as
   SDO4 V2, SDO5 V3 or SDO5 V4 by body length and published to the entities.
   **BLE connected** becomes true only after authentication **and** a valid
   overview; reaching the end of the init sequence alone is not a usable
   session.
6. If no overview arrives within `initial_status_timeout` (default 15 s), the
   session reconnects. If `status_timeout` is nonzero and the overview becomes
   older than that, telemetry is invalidated and the session reconnects before
   another command.
7. Command replies have no transaction identifier. A command timeout
   therefore drops queued motion requests and establishes a new session rather
   than guessing which reply belongs to which command.

The link is held open. The **BLE connection** switch releases it when you need
MyCentsys Pro; turn it back on afterwards. The switch is runtime-only: the link
always starts enabled at boot, and re-enabling waits for a fresh advertisement
so the key/address type are current before reconnecting.

### Link sharing

The operator accepts one BLE session at a time. Turn the **BLE connection**
switch off before using MyCentsys Pro, and back on afterwards. Do not run
any other BLE controller against the same operator at the same time.

## Testing

Host tests exercise the codec, session state machine and entity invalidation.
They use synthetic data and cannot prove compatibility with a real operator.
TLS handshakes, broker behaviour and door operation need a bench check on your own setup.

Run all four C++ host suites from the repository root:

```sh
bash tests/run_host_tests.sh
SANITIZE=1 bash tests/run_host_tests.sh  # AddressSanitizer + UndefinedBehaviorSanitizer
```

The session tests compile the real session implementation against minimal
ESPHome/ESP-IDF stubs, covering delayed replies, first-overview readiness,
freshness expiry and timer rollover. Entity tests cover invalidation and
reconnection with identical values. MQTT wrapper tests check combined
availability discovery and forwarding to the stock ESPHome MQTT components.
A firmware build is still required to check compatibility with real ESPHome
headers.

### C++ codec against the Python reference

```sh
python3 tools/generate_vectors.py
g++ -std=c++17 -Wall -Wextra -I tests tests/protocol_test.cpp \
    components/centurion_ble/protocol.cpp -o /tmp/centurion_protocol_test
/tmp/centurion_protocol_test
```

`generate_vectors.py` imports
`tools/reference/protocol.py` and writes
`tests/vectors.h`, so the C++ port cannot silently drift from the reference.

### ESPHome config and compile

```sh
esphome config centurion.yaml
esphome compile centurion.yaml
```

Validated with ESPHome **2026.9.1** on the default `esp32dev` configuration
using synthetic credentials and a test CA: configuration and full firmware
compilation pass. The build uses approximately **71.9% flash** and **58.9%
static DRAM**; certificate/configuration changes can alter these figures.
These are linker figures, not measured runtime free heap. The optional native
API-only and certificate-date-checking configurations were checked through C++
generation, not separately firmware-built or hardware-tested.

Do not reuse older flash/RAM figures: the default build no longer contains
`api` or `web_server`, but it does contain TLS and `idf_send_async`.

### Bench plan

1. **Availability matrix.** Boot with the broker up and BLE down; operator
   entities must be unavailable, `${name}/status` must be `online`, and the BLE
   state must be `OFF`. Let the BLE session become ready; operator entities
   become available and the BLE state is `ON`. Block the ESP32's broker access;
   the broker must send its LWT after detecting the lost connection. Also stop
   the broker entirely: Home Assistant's own MQTT disconnect must make the
   entities unavailable. Restore the broker and confirm
   the node birth plus the retained BLE state restore the correct result.
   Repeat with BLE down during the broker restart.
2. **Power-loss test.** With BLE connected (`ON`) and an operator entity
   available, pull the ESP32's power. The broker must publish the node LWT
   `offline`; Home Assistant must show operator entities unavailable even though
   the retained BLE topic still says `ON`. This is the `availability_mode: all`
   guarantee. Repeat after restoring power and confirm a fresh valid overview is
   required before availability returns.
3. **BLE switch recovery.** With the BLE session off, confirm the BLE connection
   switch remains available (node only) and can be turned back on. Then take the
   node offline and confirm the switch itself becomes unavailable.
4. **TLS fails closed.** With the wrong CA or a certificate whose SAN does not
   match `mqtt_broker`, the device must not connect. Confirm
   `skip_cert_cn_check: false` and exact-hostname behaviour. If certificate-date
   checking is enabled as above, also test expired/not-yet-valid certificates
   and loss of the time source after a cold boot.
5. **Optional mTLS.** If enabled, confirm the broker rejects a missing client
   certificate and accepts the configured pair; confirm the private key is not
   in version control.
6. **Retained command safety.** Clear retained command topics, then exercise
   reconnect/reboot and confirm no motion command is replayed. Verify that
   `clean_session: true` alone does not clear retained messages.
7. **Session timing.** With `logger: level: DEBUG`, confirm the first overview
   arrives inside `initial_status_timeout`. If testing `status_timeout`, measure
   the operator's real URC cadence first; do not assume a heartbeat.
8. **RAM.** Record the `esphome compile` flash/RAM report and runtime free heap
   with `idf_send_async: true` and `false`, with and without the optional API.
   Watch for TLS handshake failures under memory pressure. Do not claim numbers
   until measured on the target board.
9. **OTA.** Flash once over USB, then once over OTA with the MQTT broker
    disconnected, and confirm OTA still works without `web_server` or `api`.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| TLS handshake fails / connection refused | Wrong CA in `mqtt_ca_certificate`, expired or wrong server certificate, port not 8883, or a firewall. Check the ESPHome log for the TLS error. |
| TLS fails only for one hostname | `mqtt_broker` is not the certificate SAN. Use the exact DNS name; wildcards and IP-without-IP-SAN do not work. |
| Operator entities stay unavailable | Check both `${name}/status` and `${name}/binary_sensor/ble_connected/state`. Operator entities require `online` **and** `ON`. The BLE state stays `OFF` until authentication succeeds and a valid overview arrives. |
| Availability says online but BLE is down | The BLE state topic is stale `ON`; the component guards should publish `OFF` on link loss and expiry. Check the binary sensor state topic and its retention. `clean_session` does not clear retained messages. |
| Availability says offline after broker restart | Node status should return `online` from the birth message. If the retained BLE state is missing or `OFF`, unavailable is correct. |
| ESP32 loses power but operator entities look online | Node LWT must be on `${name}/status` and discovery must use `availability_mode: all`. A stale retained BLE `ON` alone must not keep entities available; check the component wrapper. |
| No advertisement / no key captured | Wrong MAC, operator out of range, or the tracker is scanning too passively. `scan_parameters: active: true` is required to see scan-response data. |
| MAC works once, then stops after a power cycle | The operator is using a private/rotating BLE address. `ble_client` needs a stable address; use a scanner to confirm the address type before relying on this firmware. |
| `Centurion serial service/characteristics not found` | The device is not an SDO4/SDO5, or the GATT service UUIDs differ. Check the UUIDs against [PROTOCOL.md](PROTOCOL.md). |
| Auth NACK / timeout | Phone number is not exactly the registered international string, is not registered on the operator, or the key is stale. Verify in MyCentsys Pro first. |
| Session ready but no entity updates | The controller may need extra app synchronisation before it emits an overview URC. Open the status screen in the app once, then reconnect and capture the notifications. |
| Phone app cannot connect | The ESP32 holds the only session. Turn the **BLE connection** switch off, use the app, then turn it back on. |
| State looks wrong | The firmware may use a layout not covered here. The decoder rejects unknown lengths instead of guessing; capture the URC and compare with [PROTOCOL.md](PROTOCOL.md). |
| A command repeats after reconnect | A retained motion command is on the broker. Delete it with `mosquitto_pub -r -n`; never publish OPEN/CLOSE with retain. |
| No local web page | Intentional. `web_server` was removed to eliminate the HTTP control path. Use MQTT, OTA or the fallback AP's provisioning page only. |
| Compile fails after an ESPHome update | This is an external component. Pin ESPHome until re-tested, and check the `ble_client`/`esp32_ble_tracker` APIs and the MQTT schema options used here. |

## Safety and limitations

- **Not a safety device.** The operator's beams, limits, obstruction detection
  and force settings remain the safety system.
- There is **no HTTP control interface**. MQTT is the only remote control
  transport; OTA is firmware update only. The fallback AP's captive portal is
  provisioning-only.
- Commands are momentary triggers. There is no auto-close.
- OPEN/CLOSE are refused without a fresh authenticated overview.
- A BLE link can fail. Keep a physical remote or the phone app available.
- One BLE session per operator. The phone app and this firmware cannot both
  hold the link at the same time.
- The phone number and key are stored in the device's YAML/secrets and in the
  compiled firmware. The key is reversible obfuscation, not cryptography.
  Protect the device, the broker account and your ESPHome configuration.
- TLS protects the MQTT transport; it does not make the operator a safety
  device. Use a dedicated broker account and ACLs, and never retain motion
  commands.
- This project is unofficial and not affiliated with Centurion Systems. Use at
  your own risk.

## Files you may want to change

- `centurion.yaml` — board, Wi-Fi, MQTT/TLS, entity names, session timings.
- `secrets.yaml` — Wi-Fi, MQTT credentials, CA (and optional client cert/key,
  API key). Never commit it.
- `components/centurion_ble/protocol.cpp` — frame/URC layout if your firmware
  differs.
- `components/centurion_ble/centurion_ble.cpp` — timeouts, retry policy,
  command sequence.
- `tools/generate_vectors.py` — add a vector when you capture new traffic.
