#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace esphome {

template<typename... Ts> class CallbackManager;

template<typename... Args> class CallbackManager<void(Args...)> {
 public:
  template<typename F> void add(F &&callback) {
    this->callbacks_.emplace_back(std::forward<F>(callback));
  }

  void call(Args... args) {
    for (auto &callback : this->callbacks_)
      callback(args...);
  }

 protected:
  std::vector<std::function<void(Args...)> > callbacks_;
};

inline std::string format_hex_pretty(const uint8_t *data, size_t len) {
  (void) data;
  (void) len;
  return std::string();
}

}  // namespace esphome
