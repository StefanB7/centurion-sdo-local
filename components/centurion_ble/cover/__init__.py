import esphome.codegen as cg
from esphome.components import cover
import esphome.config_validation as cv
from esphome.types import ConfigType

from .. import CONF_CENTURION_BLE_ID, CenturionBLE, centurion_ble_ns
from ..mqtt_availability import MqttAvailabilityCover, use_combined_availability

DEPENDENCIES = ["centurion_ble"]

CenturionCover = centurion_ble_ns.class_("CenturionCover", cover.Cover, cg.Component)

CONFIG_SCHEMA = (
    cover.cover_schema(CenturionCover)
    .extend({cv.Required(CONF_CENTURION_BLE_ID): cv.use_id(CenturionBLE)})
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config: ConfigType) -> None:
    use_combined_availability(config, MqttAvailabilityCover)
    hub = await cg.get_variable(config[CONF_CENTURION_BLE_ID])
    var = await cover.new_cover(config)
    cg.add(var.set_hub(hub))
    await cg.register_component(var, config)
