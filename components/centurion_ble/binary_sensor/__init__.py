import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_TYPE
from esphome.types import ConfigType

from .. import CONF_CENTURION_BLE_ID, CenturionBLE, centurion_ble_ns
from ..mqtt_availability import (
    MqttAvailabilityBinarySensor,
    use_combined_availability,
)

DEPENDENCIES = ["centurion_ble"]

CenturionBinarySensor = centurion_ble_ns.class_(
    "CenturionBinarySensor", binary_sensor.BinarySensor, cg.Component
)

# Must match CenturionBinarySensorType in
# binary_sensor/centurion_ble_binary_sensor.h.
BINARY_TYPES = {
    "connected": 0,
    "mains_on": 1,
    "battery_low": 2,
    "collision": 3,
    "max_collisions": 4,
    "stalled": 5,
    "lost": 6,
    "limits_not_set": 7,
    "xmr_alarm": 8,
}

CONFIG_SCHEMA = (
    binary_sensor.binary_sensor_schema(CenturionBinarySensor)
    .extend(
        {
            cv.Required(CONF_CENTURION_BLE_ID): cv.use_id(CenturionBLE),
            cv.Required(CONF_TYPE): cv.one_of(*BINARY_TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config: ConfigType) -> None:
    use_combined_availability(config, MqttAvailabilityBinarySensor)
    hub = await cg.get_variable(config[CONF_CENTURION_BLE_ID])
    var = await binary_sensor.new_binary_sensor(config)
    cg.add(var.set_hub(hub))
    cg.add(var.set_type(BINARY_TYPES[config[CONF_TYPE]]))
    await cg.register_component(var, config)
