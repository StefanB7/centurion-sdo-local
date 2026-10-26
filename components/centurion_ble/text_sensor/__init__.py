import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_TYPE
from esphome.types import ConfigType

from .. import CONF_CENTURION_BLE_ID, CenturionBLE, centurion_ble_ns
from ..mqtt_availability import MqttAvailabilityTextSensor, use_combined_availability

DEPENDENCIES = ["centurion_ble"]

CenturionTextSensor = centurion_ble_ns.class_(
    "CenturionTextSensor", text_sensor.TextSensor, cg.Component
)

# Must match CenturionTextSensorType in
# text_sensor/centurion_ble_text_sensor.h.
TEXT_TYPES = {
    "door_status": 0,
    "power_status": 1,
}

CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema(CenturionTextSensor)
    .extend(
        {
            cv.Required(CONF_CENTURION_BLE_ID): cv.use_id(CenturionBLE),
            cv.Required(CONF_TYPE): cv.one_of(*TEXT_TYPES, lower=True),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config: ConfigType) -> None:
    use_combined_availability(config, MqttAvailabilityTextSensor)
    hub = await cg.get_variable(config[CONF_CENTURION_BLE_ID])
    var = await text_sensor.new_text_sensor(config)
    cg.add(var.set_hub(hub))
    cg.add(var.set_type(TEXT_TYPES[config[CONF_TYPE]]))
    await cg.register_component(var, config)
