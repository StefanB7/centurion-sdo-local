#pragma once

#include <cstdint>

namespace esphome {

// The hub's inline last_status_age() uses millis(). The test never depends on
// a real clock.
inline uint32_t millis() { return 0; }

}  // namespace esphome
