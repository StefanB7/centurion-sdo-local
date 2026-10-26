#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace esphome {

// Minimal synchronous CallbackManager, enough for the hub's state callbacks.
template<typename Signature> class CallbackManager;

template<typename... Args> class CallbackManager<void(Args...)> {
 public:
  template<typename F> void add(F &&callback) {
    this->callbacks_.emplace_back(std::forward<F>(callback));
  }

  void call(Args... args) {
    for (auto &callback : this->callbacks_)
      callback(args...);
  }

 private:
  std::vector<std::function<void(Args...)>> callbacks_;
};

}  // namespace esphome
