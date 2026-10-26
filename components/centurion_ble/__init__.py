import esphome.codegen as cg
from esphome.components import ble_client, esp32_ble_tracker
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.types import ConfigType

CODEOWNERS = []
DEPENDENCIES = ["ble_client", "esp32_ble_tracker"]

CONF_PHONE = "phone"
CONF_KEY = "key"
CONF_PRODUCT_CODE = "product_code"
CONF_INITIAL_STATUS_TIMEOUT = "initial_status_timeout"
CONF_STATUS_TIMEOUT = "status_timeout"
CONF_CENTURION_BLE_ID = "centurion_ble_id"

centurion_ble_ns = cg.esphome_ns.namespace("centurion_ble")
CenturionBLE = centurion_ble_ns.class_(
    "CenturionBLE",
    cg.Component,
    ble_client.BLEClientNode,
    esp32_ble_tracker.ESPBTDeviceListener,
)


def validate_key(value: str) -> str:
    value = cv.string(value)
    cleaned = value.replace(":", "").replace("-", "").replace(" ", "")
    if not cleaned:
        return ""
    if len(cleaned) != 8:
        raise cv.Invalid("key must be exactly four bytes (8 hex characters)")
    try:
        bytes.fromhex(cleaned)
    except ValueError as err:
        raise cv.Invalid("key must be hexadecimal") from err
    return cleaned.lower()


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CenturionBLE),
            cv.Required(CONF_PHONE): cv.string_strict,
            cv.Optional(CONF_KEY, default=""): validate_key,
            cv.Optional(CONF_PRODUCT_CODE, default=0): cv.uint8_t,
            cv.Optional(
                CONF_INITIAL_STATUS_TIMEOUT, default="15s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_STATUS_TIMEOUT, default="0s"
            ): cv.positive_time_period_milliseconds,
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_phone(config[CONF_PHONE]))
    if config[CONF_KEY]:
        cg.add(var.set_key(config[CONF_KEY]))
    cg.add(var.set_product_code(config[CONF_PRODUCT_CODE]))
    cg.add(
        var.set_initial_status_timeout(
            config[CONF_INITIAL_STATUS_TIMEOUT].total_milliseconds
        )
    )
    cg.add(var.set_status_timeout(config[CONF_STATUS_TIMEOUT].total_milliseconds))

    await ble_client.register_ble_node(var, config)
    await esp32_ble_tracker.register_ble_device(var, config)
