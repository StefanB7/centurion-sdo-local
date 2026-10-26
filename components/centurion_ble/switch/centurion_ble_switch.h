#pragma once

#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"

#include "../centurion_ble.h"

#ifdef USE_ESP32

namespace esphome::centurion_ble {

class CenturionLinkSwitch : public switch_::Switch, public Component {
 public:
  void set_hub(CenturionBLE *hub) { this->hub_ = hub; }

  void setup() override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;

  CenturionBLE *hub_{nullptr};
};

}  // namespace esphome::centurion_ble

#endif  // USE_ESP32
