#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/centurion-tests.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT
cxx=${CXX:-g++}
flags=(-std=c++17 -Wall -Wextra)
if [[ ${SANITIZE:-0} == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
fi

"$cxx" "${flags[@]}" -I tests tests/protocol_test.cpp \
  components/centurion_ble/protocol.cpp -o "$build_dir/protocol_test"
"$build_dir/protocol_test"

"$cxx" "${flags[@]}" -DUSE_ESP32 -I tests/stubs -I tests \
  tests/session_test.cpp components/centurion_ble/centurion_ble.cpp \
  components/centurion_ble/protocol.cpp -o "$build_dir/session_test"
"$build_dir/session_test"

"$cxx" "${flags[@]}" -DUSE_ESP32 -I tests/entity_stubs \
  tests/entity_test.cpp \
  components/centurion_ble/binary_sensor/centurion_ble_binary_sensor.cpp \
  components/centurion_ble/sensor/centurion_ble_sensor.cpp \
  components/centurion_ble/text_sensor/centurion_ble_text_sensor.cpp \
  components/centurion_ble/cover/centurion_ble_cover.cpp \
  components/centurion_ble/protocol.cpp -o "$build_dir/entity_test"
"$build_dir/entity_test"

"$cxx" "${flags[@]}" -DUSE_MQTT -DUSE_COVER -DUSE_SENSOR \
  -DUSE_BINARY_SENSOR -DUSE_TEXT_SENSOR -I tests/mqtt_availability_stubs \
  tests/mqtt_availability_test.cpp -o "$build_dir/mqtt_availability_test"
"$build_dir/mqtt_availability_test"
