#pragma once

#include "esphome/core/component.h"
#include "esphome/components/cover/cover.h"

#include "../centurion_ble.h"

#ifdef USE_ESP32

namespace esphome::centurion_ble {

class CenturionCover : public cover::Cover, public Component {
 public:
  void set_hub(CenturionBLE *hub) { this->hub_ = hub; }

  void setup() override;
  void dump_config() override;
  cover::CoverTraits get_traits() override;

 protected:
  void control(const cover::CoverCall &call) override;
  void apply_state_(const CenturionState &state);

  CenturionBLE *hub_{nullptr};
  bool has_door_{false};
  protocol::DoorStatus last_door_{protocol::DoorStatus::UNRECOGNISED};
};

}  // namespace esphome::centurion_ble

#endif  // USE_ESP32
