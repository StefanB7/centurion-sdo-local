#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace esphome::binary_sensor {

// Mirrors the parts of ESPHome's BinarySensor that the Centurion platform uses:
// publish_state() deduplicates, invalidate_state() clears has_state().
class BinarySensor {
 public:
  virtual ~BinarySensor() = default;

  void publish_state(bool new_state) {
    const bool changed = !this->has_state_ || this->state != new_state;
    this->state = new_state;
    this->has_state_ = true;
    if (changed)
      this->notify_(new_state);
  }

  void invalidate_state() {
    if (!this->has_state_)
      return;
    this->has_state_ = false;
  }

  bool has_state() const { return this->has_state_; }

  bool state{false};

  template<typename F> void add_on_state_callback(F &&callback) {
    this->callbacks_.emplace_back(std::forward<F>(callback));
  }

 protected:
  void notify_(bool value) {
    for (auto &callback : this->callbacks_)
      callback(value);
  }

  bool has_state_{false};
  std::vector<std::function<void(bool)>> callbacks_;
};

}  // namespace esphome::binary_sensor
