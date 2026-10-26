"""Combined node + BLE MQTT availability for Centurion operator entities.

Stock ESPHome turns a per-entity ``availability:`` into the single
``availability_topic`` field, replacing the node's birth/last-will topic. The
operator entities in this component need both, expressed as a Home Assistant
``availability`` list with ``availability_mode: all``.

The C++ wrappers in ``mqtt_availability.h`` provide that. This module swaps the
generated MQTT id's type to the matching wrapper before the platform factory
(``cover.new_cover``, ``sensor.new_sensor``, ...) creates it. Entities without a
non-empty ``availability:`` override keep the stock MQTT component, and builds
without MQTT are untouched.
"""

import esphome.codegen as cg
from esphome.components import mqtt
from esphome.const import CONF_AVAILABILITY, CONF_MQTT_ID
from esphome.types import ConfigType

from . import centurion_ble_ns

# C++ names must match mqtt_availability.h.
MqttAvailabilityCover = centurion_ble_ns.class_("CenturionMqttCover", mqtt.MQTTComponent)
MqttAvailabilitySensor = centurion_ble_ns.class_("CenturionMqttSensor", mqtt.MQTTComponent)
MqttAvailabilityBinarySensor = centurion_ble_ns.class_(
    "CenturionMqttBinarySensor", mqtt.MQTTComponent
)
MqttAvailabilityTextSensor = centurion_ble_ns.class_(
    "CenturionMqttTextSensor", mqtt.MQTTComponent
)


def use_combined_availability(config: ConfigType, wrapper: cg.MockObjClass) -> None:
    """Use ``wrapper`` for this entity's MQTT id when it has BLE availability.

    Must be called before the platform factory creates the entity's MQTT
    component. Without MQTT, or without an ``availability:`` override, nothing
    changes.
    """
    mqtt_id = config.get(CONF_MQTT_ID)
    if mqtt_id is None or not config.get(CONF_AVAILABILITY):
        return
    mqtt_id.type = wrapper
