import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import modbus, select, text_sensor
from esphome.const import CONF_ID

AUTO_LOAD = ["modbus", "select", "text_sensor"]
CODEOWNERS = ["@agners"]

CONF_VENTILATION_MODE_SENSOR = "ventilation_mode_sensor"

trox_ventilation_control_ns = cg.esphome_ns.namespace("trox_ventilation_control")
TroxVentilation = trox_ventilation_control_ns.class_(
    "TroxVentilationControl", cg.PollingComponent, modbus.ModbusDevice, select.Select
)

CONFIG_SCHEMA = (
    select.SELECT_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(TroxVentilation),
            cv.Optional(CONF_VENTILATION_MODE_SENSOR): text_sensor.text_sensor_schema(),
        }
    )
    .extend(cv.polling_component_schema("10s"))
    .extend(modbus.modbus_device_schema(0x03))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await select.register_select(
        var, config, options=["Minimum", "Medium", "Maximum", "Auto"]
    )
    await cg.register_component(var, config)
    await modbus.register_modbus_device(var, config)

    if CONF_VENTILATION_MODE_SENSOR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_VENTILATION_MODE_SENSOR])
        cg.add(var.set_ventilation_mode_text_sensor(sens))
