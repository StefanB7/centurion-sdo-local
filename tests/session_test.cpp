// Host-side regression tests for the Centurion BLE session state machine.
//
// These compile the real centurion_ble.cpp against the minimal ESPHome/ESP-IDF
// stubs in tests/stubs. They cover the reply-stream safety rules: late
// replies are ignored, and telemetry only becomes usable after authentication
// plus an overview.
//
//   g++ -std=c++17 -Wall -Wextra -DUSE_ESP32 -I tests/stubs -I tests
//       tests/session_test.cpp components/centurion_ble/centurion_ble.cpp
//       components/centurion_ble/protocol.cpp -o /tmp/centurion_session_test
//   /tmp/centurion_session_test

#include "vectors.h"

#include "../components/centurion_ble/centurion_ble.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace ble_client = esphome::ble_client;
namespace espbt = esphome::esp32_ble_tracker;

static uint32_t g_now = 0;
static int failures = 0;

namespace esphome {

uint32_t millis() { return g_now; }
uint32_t micros() { return g_now * 1000; }

}  // namespace esphome

namespace {

struct WriteRecord {
  uint16_t handle;
  std::vector<uint8_t> data;
};

std::vector<WriteRecord> g_writes;

void advance(uint32_t ms) { g_now += ms; }

bool is_command(const WriteRecord &write, uint32_t address) {
  if (write.data.size() < 10 || write.data[0] != 0x02)
    return false;
  const uint32_t actual = (static_cast<uint32_t>(write.data[1]) << 24) |
                          (static_cast<uint32_t>(write.data[2]) << 16) |
                          (static_cast<uint32_t>(write.data[3]) << 8) |
                          static_cast<uint32_t>(write.data[4]);
  return actual == address;
}

}  // namespace

// Called by centurion_ble.cpp through the stub GATT header.
int esp_ble_gattc_write_char(esp_gatt_if_t gattc_if, uint16_t conn_id, uint16_t handle,
                             uint16_t value_len, uint8_t *value,
                             esp_gatt_write_type_t write_type, esp_gatt_auth_req_t auth_req) {
  (void) gattc_if;
  (void) conn_id;
  (void) write_type;
  (void) auth_req;
  g_writes.push_back({handle, std::vector<uint8_t>(value, value + value_len)});
  return ESP_OK;
}

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
      failures++;                                                         \
    }                                                                     \
  } while (0)

class TestHub : public esphome::centurion_ble::CenturionBLE {
 public:
  using SessionState = esphome::centurion_ble::CenturionBLE::SessionState;

  void attach(ble_client::BLEClient *client) {
    this->esphome::ble_client::BLEClientNode::parent_ = client;
  }

  SessionState session() const { return this->session_state_; }
  bool pending() const { return this->command_pending_; }
  size_t queued() const { return this->queue_count_; }
  uint16_t notify_handle() const { return this->notify_handle_; }
};

namespace {

const uint8_t COMPLETE[] = {0x00, 0x00};

void notify_bytes(TestHub &hub, uint16_t handle, const uint8_t *data, size_t len) {
  esp_ble_gattc_cb_param_t param{};
  param.notify.handle = handle;
  param.notify.value = const_cast<uint8_t *>(data);
  param.notify.value_len = static_cast<uint16_t>(len);
  hub.gattc_event_handler(ESP_GATTC_NOTIFY_EVT, 0, &param);
}

void notify(TestHub &hub, const uint8_t *data, size_t len) {
  notify_bytes(hub, hub.notify_handle(), data, len);
}

void send_overview(TestHub &hub) {
  notify(hub, vectors::SDO4_V2, sizeof(vectors::SDO4_V2));
}

void discover(TestHub &hub) {
  esp_ble_gattc_cb_param_t param{};
  hub.gattc_event_handler(ESP_GATTC_SEARCH_CMPL_EVT, 0, &param);

  param = {};
  param.reg_for_notify.handle = hub.notify_handle();
  param.reg_for_notify.status = ESP_GATT_OK;
  hub.gattc_event_handler(ESP_GATTC_REG_FOR_NOTIFY_EVT, 0, &param);
}

void reset_world(TestHub &hub, ble_client::BLEClient &client) {
  g_now = 0;
  g_writes.clear();
  client.disconnect_calls = 0;
  client.set_connected(true);

  hub.attach(&client);
  hub.set_phone("+27821234567");
  hub.set_key("11223344");
  hub.set_product_code(39);
  hub.setup();
  discover(hub);
}

// Drives WAIT_DELAY through the four authentication commands. Each command is
// completed with 00 00 before the next one is queued. With inject_overview_at
// set, a status overview arrives before that command's completion: before the
// auth command it must be ignored, after it must be buffered until the init
// sequence ends.
void authenticate(TestHub &hub, int inject_overview_at = -1) {
  advance(1600);
  hub.loop();

  bool injected = false;
  for (int i = 0; i < 12 && hub.session() == TestHub::SessionState::INIT; i++) {
    hub.loop();
    if (!hub.pending())
      continue;
    if (inject_overview_at >= 0 && !injected && i == inject_overview_at) {
      send_overview(hub);
      injected = true;
    }
    notify(hub, COMPLETE, sizeof(COMPLETE));
  }
}

void make_ready(TestHub &hub) {
  authenticate(hub);
  send_overview(hub);
}

void test_early_urc_ignored() {
  TestHub hub;
  ble_client::BLEClient client;
  reset_world(hub, client);

  authenticate(hub, 1);
  CHECK(hub.session() == TestHub::SessionState::WAIT_STATUS);
  CHECK(!hub.state().connected);
  CHECK(!hub.state().door_known);

  // Authentication completing is not enough; the first accepted overview is
  // what makes the session usable.
  send_overview(hub);
  CHECK(hub.session() == TestHub::SessionState::READY);
  CHECK(hub.state().connected);
  CHECK(hub.state().door_known);
}

void test_authenticated_overview_before_init_end() {
  TestHub hub;
  ble_client::BLEClient client;
  reset_world(hub, client);

  // The overview arrives after AuthMobilePhoneA but before the last init
  // command completes. It is valid, so it must be used rather than dropped.
  authenticate(hub, 2);
  CHECK(hub.session() == TestHub::SessionState::READY);
  CHECK(hub.state().connected);
  CHECK(hub.state().door_known);
  CHECK(hub.queued() == 0);
  CHECK(!hub.pending());
}

void test_cached_overview_expires_during_slow_init() {
  TestHub hub;
  ble_client::BLEClient client;
  reset_world(hub, client);
  hub.set_status_timeout(500);

  advance(1600);
  hub.loop();  // ChangeBLEConnectionParameters
  CHECK(hub.pending());
  notify(hub, COMPLETE, sizeof(COMPLETE));

  hub.loop();  // AuthMobilePhoneA
  CHECK(hub.pending());
  notify(hub, COMPLETE, sizeof(COMPLETE));

  hub.loop();  // DisableURCBlock
  CHECK(hub.pending());

  // Cache an authenticated overview shortly before a 32-bit millis() wrap.
  g_now = 0xFFFFFF00u;
  send_overview(hub);
  CHECK(hub.session() == TestHub::SessionState::INIT);
  CHECK(!hub.state().connected);

  // The remaining init commands complete slowly, so the cached overview is
  // older than status_timeout by the time WAIT_STATUS is reached.
  advance(0x300);  // wraps to 0x00000200
  notify(hub, COMPLETE, sizeof(COMPLETE));
  hub.loop();  // EnterStatusOverview
  CHECK(hub.pending());
  notify(hub, COMPLETE, sizeof(COMPLETE));

  CHECK(hub.session() == TestHub::SessionState::WAIT_STATUS);
  CHECK(!hub.state().connected);
  CHECK(!hub.state().door_known);
  CHECK(hub.last_status_age() == UINT32_MAX);

  // A stale cache must not make commands usable.
  hub.open();
  CHECK(hub.queued() == 0);

  // A genuinely fresh overview is still accepted.
  send_overview(hub);
  CHECK(hub.session() == TestHub::SessionState::READY);
  CHECK(hub.state().connected);
  CHECK(hub.state().door_known);
  CHECK(hub.last_status_age() == 0);
}

void test_initial_status_timeout() {
  TestHub hub;
  ble_client::BLEClient client;
  reset_world(hub, client);

  authenticate(hub);
  CHECK(hub.session() == TestHub::SessionState::WAIT_STATUS);
  CHECK(!hub.state().connected);

  advance(14000);
  hub.loop();
  CHECK(hub.session() == TestHub::SessionState::WAIT_STATUS);
  CHECK(client.disconnect_calls == 0);

  advance(1200);
  hub.loop();
  CHECK(hub.session() == TestHub::SessionState::DISCONNECTING);
  CHECK(!hub.state().connected);

  hub.loop();
  CHECK(client.disconnect_calls == 1);
  CHECK(!client.connected());
}

void test_freshness_timeout() {
  TestHub hub;
  ble_client::BLEClient client;
  reset_world(hub, client);
  make_ready(hub);

  hub.set_status_timeout(1000);
  advance(500);
  CHECK(hub.last_status_age() == 500);

  g_writes.clear();
  hub.open();
  CHECK(hub.queued() == 1);
  hub.loop();
  CHECK(hub.pending());
  CHECK(!g_writes.empty() && is_command(g_writes.back(), esphome::centurion_ble::protocol::CMD_TRIGGER));
  notify(hub, COMPLETE, sizeof(COMPLETE));
  CHECK(!hub.pending());

  advance(600);
  CHECK(hub.last_status_age() == 1100);

  // Stale telemetry must not accept new motion.
  g_writes.clear();
  hub.close();
  CHECK(hub.queued() == 0);

  hub.loop();
  CHECK(hub.session() == TestHub::SessionState::DISCONNECTING);
  CHECK(!hub.state().connected);
  CHECK(!hub.state().door_known);
  CHECK(hub.last_status_age() == UINT32_MAX);
  CHECK(g_writes.empty());

  hub.loop();
  CHECK(client.disconnect_calls == 1);
}

void test_freshness_rollover() {
  TestHub hub;
  ble_client::BLEClient client;
  reset_world(hub, client);

  // Place the overview near the 32-bit millis() wrap point.
  g_now = 0xFFFFF8C0u;
  authenticate(hub);
  send_overview(hub);
  CHECK(hub.session() == TestHub::SessionState::READY);

  hub.set_status_timeout(1000);
  advance(0x200);  // wraps to 0x00000100
  CHECK(hub.last_status_age() == 0x200);

  hub.loop();
  CHECK(hub.session() == TestHub::SessionState::READY);
  CHECK(hub.state().connected);

  advance(0x400);
  CHECK(hub.last_status_age() == 0x600);
  hub.loop();
  CHECK(hub.session() == TestHub::SessionState::DISCONNECTING);
  CHECK(!hub.state().connected);
}

void test_disabled_link_ignores_notifications() {
  TestHub hub;
  ble_client::BLEClient client;
  reset_world(hub, client);
  make_ready(hub);

  const uint16_t old_notify_handle = hub.notify_handle();
  hub.set_link_enabled(false);
  CHECK(hub.session() == TestHub::SessionState::IDLE);
  CHECK(!hub.state().connected);
  CHECK(client.disconnect_calls == 1);

  notify_bytes(hub, old_notify_handle, vectors::SDO4_V2, sizeof(vectors::SDO4_V2));
  CHECK(!hub.state().connected);
  CHECK(!hub.state().door_known);

  // Re-enabling waits for discovery; an old notification must still be stale.
  hub.set_link_enabled(true);
  notify_bytes(hub, old_notify_handle, vectors::SDO4_V2, sizeof(vectors::SDO4_V2));
  CHECK(!hub.state().connected);
  CHECK(!hub.state().door_known);
  CHECK(hub.session() == TestHub::SessionState::IDLE);
}

}  // namespace

int main() {
  test_early_urc_ignored();
  test_authenticated_overview_before_init_end();
  test_cached_overview_expires_during_slow_init();
  test_initial_status_timeout();
  test_freshness_timeout();
  test_freshness_rollover();
  test_disabled_link_ignores_notifications();

  if (failures == 0) {
    std::printf("all session regression tests passed\n");
    return 0;
  }
  std::printf("%d session check(s) failed\n", failures);
  return 1;
}
