#pragma once

// Logging is irrelevant to the state logic under test.
#define ESP_LOGE(tag, ...) \
  do {                     \
  } while (0)
#define ESP_LOGW(tag, ...) \
  do {                     \
  } while (0)
#define ESP_LOGI(tag, ...) \
  do {                     \
  } while (0)
#define ESP_LOGD(tag, ...) \
  do {                     \
  } while (0)
#define ESP_LOGV(tag, ...) \
  do {                     \
  } while (0)

#define LOG_BINARY_SENSOR(prefix, type, obj) \
  do {                                       \
  } while (0)
#define LOG_SENSOR(prefix, type, obj) \
  do {                                \
  } while (0)
#define LOG_TEXT_SENSOR(prefix, type, obj) \
  do {                                     \
  } while (0)
#define LOG_COVER(prefix, type, obj) \
  do {                               \
  } while (0)
