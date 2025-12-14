import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, remote_base, remote_receiver, sensor
from esphome.const import (
    CONF_BATTERY_LEVEL,
    CONF_CHANNEL,
    CONF_DEVICE_ID,
    CONF_FORCE_UPDATE,
    CONF_HUMIDITY,
    CONF_ID,
    CONF_NAME,
    CONF_SENSORS,
    CONF_TEMPERATURE,
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)

CODEOWNERS = ["@anna-oake"]
DEPENDENCIES = ["remote_receiver"]
AUTO_LOAD = ["sensor", "binary_sensor"]

vauno_ns = cg.esphome_ns.namespace("vauno")
VaunoComponent = vauno_ns.class_("VaunoComponent", cg.Component)


def _inherit_device_id(cfg):
    parent = cfg.get(CONF_DEVICE_ID)
    if parent is None:
        return cfg

    for key in (CONF_TEMPERATURE, CONF_HUMIDITY, CONF_BATTERY_LEVEL):
        child = cfg.get(key)
        if child is None:
            child = {}
            cfg[key] = child
        child.setdefault(CONF_DEVICE_ID, parent)

    return cfg


SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.hex_int_range(max=0xFF),
        cv.Required(CONF_CHANNEL): cv.int_range(min=1, max=3),
        cv.Optional(CONF_DEVICE_ID): cv.sub_device_id,
        cv.Optional(CONF_TEMPERATURE, default={}): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT,
            filters=[
                {
                    "timeout": cv.TimePeriod(minutes=5),
                },
            ],
        ).extend(
            {
                cv.Optional(CONF_FORCE_UPDATE, default=True): cv.boolean,
                cv.Optional(CONF_NAME, default="Temperature"): cv.string,
            }
        ),
        cv.Optional(CONF_HUMIDITY, default={}): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_HUMIDITY,
            state_class=STATE_CLASS_MEASUREMENT,
            filters=[
                {
                    "timeout": cv.TimePeriod(minutes=5),
                },
            ],
        ).extend(
            {
                cv.Optional(CONF_FORCE_UPDATE, default=True): cv.boolean,
                cv.Optional(CONF_NAME, default="Humidity"): cv.string,
            }
        ),
        cv.Optional(CONF_BATTERY_LEVEL, default={}): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_BATTERY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            filters=[
                {
                    "timeout": cv.TimePeriod(minutes=5),
                },
            ],
        ).extend(
            {
                cv.Optional(CONF_NAME, default="Battery"): cv.string,
            }
        ),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(VaunoComponent),
        cv.GenerateID(remote_base.CONF_RECEIVER_ID): cv.use_id(
            remote_receiver.RemoteReceiverComponent
        ),
        cv.Optional(CONF_SENSORS): cv.ensure_list(
            cv.All(_inherit_device_id, SENSOR_SCHEMA)
        ),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await remote_base.register_listener(var, config)

    if sensors := config.get(CONF_SENSORS):
        for sens in sensors:
            temp = await sensor.new_sensor(sens[CONF_TEMPERATURE])
            hum = await sensor.new_sensor(sens[CONF_HUMIDITY])
            bat = await binary_sensor.new_binary_sensor(sens[CONF_BATTERY_LEVEL])
            cg.add(var.add_device(sens[CONF_ID], sens[CONF_CHANNEL], temp, hum, bat))
