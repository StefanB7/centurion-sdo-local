#pragma once

#include <string>

#include "esphome/core/component.h"

namespace esphome::mqtt {

struct Availability {
  std::string topic;
  std::string payload_available;
  std::string payload_not_available;
};

class MQTTClientComponent {
 public:
  const Availability &get_availability() { return this->availability; }
  Availability availability;
};

extern MQTTClientComponent *global_mqtt_client;

}  // namespace esphome::mqtt
