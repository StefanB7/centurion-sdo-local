#pragma once

#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace esphome::sensor {

// Mirrors the parts of ESPHome's Sensor that the Centurion platform uses.
class Sensor {
 public:
  virtual ~Sensor() = default;

  void publish_state(float value) {
    this->state = value;
    this->has_state_ = true;
    for (auto &callback : this->callbacks_)
      callback(value);
  }

  bool has_state() const { return this->has_state_; }

  float state{NAN};

  template<typename F> void add_on_state_callback(F &&callback) {
    this->callbacks_.emplace_back(std::forward<F>(callback));
  }

 protected:
  bool has_state_{false};
  std::vector<std::function<void(float)>> callbacks_;
};

}  // namespace esphome::sensor
