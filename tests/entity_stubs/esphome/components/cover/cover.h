#pragma once

#include <cstdint>
#include <optional>

namespace esphome::cover {

enum CoverOperation : uint8_t {
  COVER_OPERATION_IDLE = 0,
  COVER_OPERATION_OPENING = 1,
  COVER_OPERATION_CLOSING = 2,
};

inline constexpr float COVER_OPEN = 1.0f;
inline constexpr float COVER_CLOSED = 0.0f;

class CoverTraits {
 public:
  void set_supports_stop(bool value) { this->supports_stop_ = value; }
  void set_supports_position(bool value) { this->supports_position_ = value; }
  void set_supports_tilt(bool value) { this->supports_tilt_ = value; }
  void set_is_assumed_state(bool value) { this->assumed_state_ = value; }

 private:
  bool supports_stop_{false};
  bool supports_position_{false};
  bool supports_tilt_{false};
  bool assumed_state_{false};
};

class CoverCall {
 public:
  bool get_stop() const { return this->stop_; }
  std::optional<float> get_position() const { return this->position_; }

  void set_stop(bool stop) { this->stop_ = stop; }
  void set_position(float position) { this->position_ = position; }

 private:
  bool stop_{false};
  std::optional<float> position_{};
};

// Mirrors the parts of ESPHome's Cover that the Centurion platform uses.
class Cover {
 public:
  virtual ~Cover() = default;

  virtual CoverTraits get_traits() { return {}; }
  virtual void control(const CoverCall &) {}

  void publish_state(bool /*open*/) { this->publish_count++; }

  float position{0.5f};
  CoverOperation current_operation{COVER_OPERATION_IDLE};
  int publish_count{0};
};

}  // namespace esphome::cover
