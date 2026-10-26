# Centurion SMART garage Bluetooth protocol

Source: MyCentsys Pro Android **1.5.0.218**, package
`com.CenturionSystems.MyCentsysPro`, version code 374.

## What is established

The findings below come from static analysis of the app. The core flow
(connect, authenticate, status overview, open/close triggers) has since been
confirmed working against an SDO5 SMART+ (SDO4 is untested). Details not exercised on hardware are
marked as not yet established.

The app explicitly supports both `SDO4SMART` (product code **13 / 0x0D**) and
`SDO5SMART_PLUS` (**39 / 0x27**). An advertisement can distinguish these without
relying on the housing's label. The commercial model name remains unconfirmed.

## 1. GATT transport

| Purpose | UUID |
|---|---|
| Serial service | `57b53956-da15-4005-8a1f-fcacde09af51` |
| Phone → controller write | `57b5f92c-da15-4005-8a1f-fcacde09af51` |
| Controller → phone notify | `57b5db3b-da15-4005-8a1f-fcacde09af51` |

The app writes **without GATT response**, and subscribes to notifications on the
other characteristic. Application-level replies provide transaction completion.
It requests MTU **120** for SDO4 and **512** for SDO5 PLUS, waits 1.5 seconds,
discovers the two characteristics, then subscribes. Configured protocol packet
limits are 100 and 502 bytes respectively; chunk limits are 1024 and 4096.
Do not confuse requested MTU with the negotiated MTU or the application's limits.

The scanner also includes Nordic DFU service `0000fe59-0000-1000-8000-00805f9b34fb`.
That is a firmware-update path, separate from the normal serial service.

## 2. Advertising data

`TransferProtocol.ParseCentSysAdvertisingData` consumes
`Shiny.AdvertisementData.ManufacturerData.Data`, with two branches:

| Field | 18-byte data branch | 20-byte data branch |
|---|---:|---:|
| Serial 1, signed little-endian int32 | 1–4 | 1–4 |
| Serial 2, signed little-endian int32 | 5–8 | 5–8 |
| Four authentication-key bytes | 10–13 | 12–15 |
| Connection state | 14 | 16 |
| Firmware major | 15 | 17 |
| Firmware minor | 16 | 18 |
| Product code | 17 | 19 |

Unused bytes in these branches have not been assigned meanings. The app calls
the four-byte key `DeviceMacAddress`; it is **not** a six-byte Bluetooth address.
Do not derive it by truncating the address printed by a BLE scanner.

Connection states: 0 user connected, 1 filesystem busy, 2 factory test, 3 DFU,
4 remote, 255 OK. These are controller-advertised states, not GATT status codes.

Scanner libraries differ in whether they expose the manufacturer company ID
separately. These offsets are into the **app's byte array**, not an entire
advertising packet or AD element. Verify the byte-array boundaries in a capture;
do not blindly prepend or strip two bytes to force a match.

## 3. Outgoing frame

The normal outgoing header is **10 bytes**:

| Offset | Size | Meaning |
|---:|---:|---|
| 0 | 1 | Transfer type |
| 1 | 4 | Command or logical address, **big-endian** |
| 5 | 4 | Payload/request size, **big-endian** |
| 9 | 1 | Last-chunk flag (0 for the normal commands examined) |
| 10 | N | Payload, if any |

Transfer types: 0 read request, 1 write request, 2 command with response,
3 command without response. The normal app command path uses type 2.
Read requests use the size field for the requested byte count, with no appended
payload. Ordinary parameterless commands use size 0. A few internal commands
have special padding/size defaults; mirror their code instead of assuming all
parameterless internal commands are identical.

No additional application checksum or blanket payload encryption is applied by
`SendPacket` in this BLE path. File transfer has separate CRC-related commands
and framing; this does not establish whether the BLE link itself is encrypted.

Selected addresses, shared between the examined SDO4/SDO5 dictionaries:

| Command | Address |
|---|---|
| AuthMobilePhoneA | `0x08000004` |
| AuthMobilePhoneB | `0x08000005` |
| AuthMobilePhoneFromLCDSingleSession | `0x08000006` |
| AuthMobilePhoneFromLCDPermanentsession | `0x08000007` |
| EnterStatusOverviewMode | `0x08000022` |
| ExitStatusOverviewMode | `0x08000023` |
| EnterStatusIoMode | `0x08000024` |
| ExitStatusIoMode | `0x08000025` |
| EnableURCBlock | `0x0800002C` |
| DisableURCBlock | `0x0800002D` |
| ChangeBLEConnectionParameters | `0x0FFFFFFC` |

### Normal authenticated session, as observed in the code

1. Discover product and four-byte key from advertising data.
2. Connect, negotiate MTU, subscribe to the notify characteristic.
3. Send ChangeBLEConnectionParameters. Android parameters are four big-endian
   uint16s: 10, 12, 0, 4000. These are the raw values in the app, not converted
   wall-clock units.
4. Wait for application completion, then send AuthMobilePhoneA with the encoded
   registered international phone-number string.
5. A completion response marks authentication successful. A NACK in this state
   is interpreted as the number not being registered on the operator.
6. The full app performs subsequent configuration/version synchronization.
   To enable SDO telemetry, `OperatorStatusService` first sends DisableURCBlock,
   then EnterStatusOverviewMode, awaiting each command's completion.
7. Decode status URCs on the same notify characteristic.

The proposed short path from authentication directly to overview mode still
needs testing on the actual controller; the full app does more synchronization.
Only one transaction is in progress at a time. Command timeout is 7 seconds;
the connected app may retry up to three times.

Example packets generated from the serializers (not live captures):

```text
Change connection parameters:
02 0f ff ff fc 00 00 00 08 00 00 0a 00 0c 00 00 0f a0

Disable URC blocking:
02 08 00 00 2d 00 00 00 00 00

Enter status overview:
02 08 00 00 22 00 00 00 00 00
```

## 4. Phone-number authentication encoding

`EncryptPhoneNumber` takes UTF-8 bytes of the supplied phone string, XORs each
with a repeating four-byte advertised key, then swaps the resulting nibbles:

```python
x = phone_utf8[i] ^ advertised_key[i % 4]
encoded[i] = ((x << 4) | (x >> 4)) & 0xff
```

This is reversible obfuscation, not a cryptographic challenge/response in the
examined normal authentication path. It does not make an unregistered number
authorized. The call site passes `registeredUser.InternationalNumber`; the
encoding function itself performs no phone-number normalization or terminator
padding. Preserve the exact stored international string when reproducing it.

The app has a separate physical-confirmation/manual-registration flow using
AuthMobilePhoneB, connection-status RAM reads at `0x00900200`, button prompts and
single/permanent-session commands. See `PrepareAndSendNextRamReadRequestPacket`,
`processInfraTpBuffer`, and `SmartCommunicationManager.ConnectAndAuthenticateAsync`.
That flow has not been implemented in the offline helpers.

## 5. Incoming ordinary replies and unsolicited status (URC)

Ordinary frames start with a **two-byte** header:

```text
[transfer_status: u8] [payload_size: u8] [payload...]
```

Status values: 0 complete, 1 read reply, 2 ACK, 3 NACK, 4 timeout.
The examined command state machine completes commands on **status 0**, normally
`00 00`; do not substitute status 2 merely because its enum name is ACK.

A URC payload includes another two-byte buffer header. The app dispatches using
the first byte and strips both before deserializing the body:

```text
[0] [payload_size] [buffer_state] [buffer_metadata] [body...]
```

The second buffer byte is not required by the status handler; its precise status
URC meaning is not yet established. Do not assume it is always zero. A RAM-read
reply can also contain a buffer header: interpretation depends on transaction
state, so an arbitrary successful RAM response is not necessarily a URC.

Relevant buffer states: **140 / 0x8C** SDO status overview, **146 / 0x92** SDO I/O
overview, **164 / 0xA4** extended SDO overview, **165 / 0xA5** extended SDO I/O.

File reads are an exception to this framing: the first packet of a chunk starts
with status + four-byte big-endian logical address, followed by file data;
subsequent notifications can be raw continuation bytes. The offline notification
decoder is for ordinary replies/URCs, not file-transfer reassembly.

## 6. SDO overview body

The structures are packed with alignment 1. Multi-byte body fields are
**little-endian**, unlike the outgoing address/size header.

| Field | SDO4 V2 offset | SDO5 V3/V4 offset | Type |
|---|---:|---:|---|
| Notification flags 1 | 0 | 0 | uint32 |
| Battery voltage | 4 | 4 | uint16, divide by **10** for volts |
| Condition flags | 6 | 6 | uint8 |
| Seconds remaining | 7 | 8 | uint8 SDO4 / uint16 SDO5 |
| Timer flags | 8 | 10 | uint8, bit 0 timer active |
| Door status | 9 | 12 | uint8 |
| Closing-beam status | 10 | 13 | uint8, product-specific enum |
| XMR status | 11 | 14 | uint8 |
| Power status | 12 | 15 | uint8 |
| Feedback handle | 13 | 16 | uint8 |
| Trigger handle | 14 | 17 | uint8 |
| Feedback index | 16 | 20 | uint16 |
| Trigger index | 18 | 22 | uint16 |
| Notification flags 2 | — | 24 (V4 only) | uint32 |
| Future/reserved | — | 28–43 (V4 only) | 16 bytes |

Body sizes: SDO4 V2 **20**, SDO5 V3 **24**, SDO5 V4 **44** bytes. Full ordinary
URC sizes including both headers are therefore 24, 28 and 48 bytes.
The app also contains an older 20-byte SDO4 V1 structure with different offsets;
the current `OperatorStatusService` SDO4 path uses V2. The helper intentionally
requires a layout instead of guessing from a 20-byte body.

Door enum: 1 open, 2 opening, 3 closed, 4 closing, 5 partially open, 6 learn,
7 lost, 255 unknown. Power enum: 0 normal, 1 low, 2 unknown, 3 mains off.

Useful notification flag masks in the examined overview classes:

| Mask | Meaning when set |
|---|---|
| `0x00000001` | Collision detected |
| `0x00000020` | Maximum collisions reached |
| `0x00000080` | Stalled |
| `0x00000200` | Low battery |
| `0x00000400` | Lost |
| `0x00000800` | Limits not set |
| `0x00004000` | XMR alarm |
| `0x00010000` | Mains off (mains-on helper inverts this bit) |

## 7. Local tools and verification

`tools/centurion_protocol.py` uses only the Python standard library and performs
**offline decoding/packet construction**:

```sh
python3 tools/centurion_protocol.py command 0x08000022
python3 tools/centurion_protocol.py notification '00 00'
python3 tools/centurion_protocol.py advertisement '<manufacturer-data hex>'
python3 tools/centurion_protocol.py notification '<notification hex>' --layout sdo5-v3
python3 tools/centurion_protocol.py auth --key '<four advertised bytes in hex>'
python3 -m unittest discover -s tools -p 'test_*.py' -v
```

The auth helper prompts for the exact phone string locally. Packet output itself
contains its reversibly encoded value. Six tests cover fixed synthetic frames,
mixed byte orders, voltage scaling, layout differences, auth encoding and
malformed lengths. They do not constitute hardware validation.

Assembly extraction is reproducible with the checked-in helper (uses system
`liblz4`; format reference: https://github.com/bishopfox/mauidll):

```sh
python3 tools/extract_assemblies.py extracted/config.arm64_v8a/lib/arm64-v8a/libassembly-store.so assemblies
```

## 8. Next evidence needed

A short successful MyCentsys Pro Bluetooth session will establish the actual
product, phone string encoding, negotiated MTU, notification lengths, and whether
the short authenticated telemetry sequence works on this firmware.

On an Android phone, enable Bluetooth HCI snoop logging in Developer options
(select full logging if that option is offered), restart Bluetooth if required,
then connect using MyCentsys Pro and view the status screen for about 20 seconds.
Record the approximate connection time and model/firmware shown by the app.
Produce an Android bug report via Developer options; depending on the phone,
it contains a `btsnoop_hci.log` or similarly named Bluetooth capture. Capture
availability and paths vary by vendor. Turn snoop logging off afterwards.

The Bluetooth capture, or exported ATT write/notification values for the UUIDs
above, is enough for the next pass; a whole bug report contains unrelated device
information. Door movement is not needed to verify connection and basic telemetry.

Still unverified: actual controller model/firmware, precise manufacturer-data
boundaries for the capture tool, link bonding/encryption requirements, whether
extra app synchronization is required before overview mode, telemetry cadence,
and persistent-session/reconnection behavior. Scan-response and live ATT traffic
will resolve these without guessing from the app code alone.
