import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.types import ConfigType

from .. import CONF_CENTURION_BLE_ID, CenturionBLE, centurion_ble_ns

DEPENDENCIES = ["centurion_ble"]

CenturionLinkSwitch = centurion_ble_ns.class_(
    "CenturionLinkSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = (
    switch.switch_schema(CenturionLinkSwitch)
    .extend({cv.Required(CONF_CENTURION_BLE_ID): cv.use_id(CenturionBLE)})
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_CENTURION_BLE_ID])
    var = await switch.new_switch(config)
    cg.add(var.set_hub(hub))
    await cg.register_component(var, config)
