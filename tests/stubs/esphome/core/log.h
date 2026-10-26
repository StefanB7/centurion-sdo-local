#pragma once

// The session tests only care about behaviour, not log text. Passing every
// argument through this helper keeps the real code's parameters "used" under
// -Wunused-parameter without evaluating printf formatting.
template<typename... Ts> inline void stub_log_consume(const Ts &...) {}

#define ESP_LOGE(...) stub_log_consume(__VA_ARGS__)
#define ESP_LOGW(...) stub_log_consume(__VA_ARGS__)
#define ESP_LOGI(...) stub_log_consume(__VA_ARGS__)
#define ESP_LOGD(...) stub_log_consume(__VA_ARGS__)
#define ESP_LOGV(...) stub_log_consume(__VA_ARGS__)
#define ESP_LOGCONFIG(...) stub_log_consume(__VA_ARGS__)
