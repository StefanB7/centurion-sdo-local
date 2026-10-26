import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import CONF_TYPE
from esphome.types import ConfigType

from .. import CONF_CENTURION_BLE_ID, CenturionBLE, centurion_ble_ns
from ..mqtt_availability import MqttAvailabilitySensor, use_combined_availability

DEPENDENCIES = ["centurion_ble"]

CenturionSensor = centurion_ble_ns.class_("CenturionSensor", sensor.Sensor, cg.Component)

# Must match CenturionSensorType in sensor/centurion_ble_sensor.h.
SENSOR_TYPES = {
    "battery_voltage": 0,
    "timer_remaining": 1,
    "beam_raw": 2,
}

CONFIG_SCHEMA = (
    sensor.sensor_schema(CenturionSensor)
    .extend(
        {
            cv.Required(CONF_CENTURION_BLE_ID): cv.use_id(CenturionBLE),
            cv.Required(CONF_TYPE): cv.one_of(*SENSOR_TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config: ConfigType) -> None:
    use_combined_availability(config, MqttAvailabilitySensor)
    hub = await cg.get_variable(config[CONF_CENTURION_BLE_ID])
    var = await sensor.new_sensor(config)
    cg.add(var.set_hub(hub))
    cg.add(var.set_type(SENSOR_TYPES[config[CONF_TYPE]]))
    await cg.register_component(var, config)
