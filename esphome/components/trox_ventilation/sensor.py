import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, modbus
from esphome.const import CONF_ID

CONF_DROSSELSTELLUNG = "drosselstellung"
CONF_VOLUMENSTROM_PROZENT = "volumenstrom_prozent"
CONF_DRUCK_DROSSELORGAN = "druck_drosselorgan"
CONF_VOLUMENSTROM = "volumenstrom"
CONF_TEMPERATUR = "temperatur"
CONF_VOC = "voc"

AUTO_LOAD = ["modbus"]
CODEOWNERS = ["@agners"]

trox_ventilation_ns = cg.esphome_ns.namespace("trox_ventilation")
TroxVentilation = trox_ventilation_ns.class_(
    "TroxVentilation", cg.PollingComponent, modbus.ModbusDevice
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TroxVentilation),
            cv.Optional(CONF_DROSSELSTELLUNG): sensor.sensor_schema(),
            cv.Optional(CONF_VOLUMENSTROM_PROZENT): sensor.sensor_schema(),
            cv.Optional(CONF_DRUCK_DROSSELORGAN): sensor.sensor_schema(),
            cv.Optional(CONF_VOLUMENSTROM): sensor.sensor_schema(),
            cv.Optional(CONF_TEMPERATUR): sensor.sensor_schema(),
            cv.Optional(CONF_VOC): sensor.sensor_schema(),
        }
    )
    .extend(cv.polling_component_schema("10s"))
    .extend(modbus.modbus_device_schema(0x02))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await modbus.register_modbus_device(var, config)

    if CONF_DROSSELSTELLUNG in config:
        sens = await sensor.new_sensor(config[CONF_DROSSELSTELLUNG])
        cg.add(var.set_drosselstellung_sensor(sens))

    if CONF_VOLUMENSTROM_PROZENT in config:
        sens = await sensor.new_sensor(config[CONF_VOLUMENSTROM_PROZENT])
        cg.add(var.set_volumenstrom_prozent_sensor(sens))

    if CONF_DRUCK_DROSSELORGAN in config:
        sens = await sensor.new_sensor(config[CONF_DRUCK_DROSSELORGAN])
        cg.add(var.set_druck_drosselorgan_sensor(sens))

    if CONF_VOLUMENSTROM in config:
        sens = await sensor.new_sensor(config[CONF_VOLUMENSTROM])
        cg.add(var.set_volumenstrom_sensor(sens))

    if CONF_TEMPERATUR in config:
        sens = await sensor.new_sensor(config[CONF_TEMPERATUR])
        cg.add(var.set_temperatur_sensor(sens))

    if CONF_VOC in config:
        sens = await sensor.new_sensor(config[CONF_VOC])
        cg.add(var.set_voc_sensor(sens))
