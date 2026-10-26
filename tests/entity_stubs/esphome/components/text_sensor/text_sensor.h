#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace esphome::text_sensor {

// Mirrors the parts of ESPHome's TextSensor that the Centurion platform uses.
class TextSensor {
 public:
  virtual ~TextSensor() = default;

  void publish_state(const std::string &value) { this->publish_state(value.c_str()); }

  void publish_state(const char *value) {
    this->state = value;
    this->has_state_ = true;
    for (auto &callback : this->callbacks_)
      callback(this->state);
  }

  bool has_state() const { return this->has_state_; }

  std::string state;

  template<typename F> void add_on_state_callback(F &&callback) {
    this->callbacks_.emplace_back(std::forward<F>(callback));
  }

 protected:
  bool has_state_{false};
  std::vector<std::function<void(const std::string &)>> callbacks_;
};

}  // namespace esphome::text_sensor
