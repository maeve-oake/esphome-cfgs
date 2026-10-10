import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, output
from esphome.const import CONF_OUTPUT_ID

CONF_OUTPUTS = "outputs"
CONF_MIN_BRIGHTNESS = "min_brightness"
CONF_MAX_BRIGHTNESS = "max_brightness"
ranged_light_ns = cg.esphome_ns.namespace("ranged_light")
RangedLightOutput = ranged_light_ns.class_("RangedLightOutput", light.LightOutput)


def validate_range(config):
    if config[CONF_MIN_BRIGHTNESS] > config[CONF_MAX_BRIGHTNESS]:
        raise cv.Invalid("min_brightness must not exceed max_brightness")
    return config


CONFIG_SCHEMA = cv.All(
    light.BRIGHTNESS_ONLY_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(RangedLightOutput),
            cv.Required(CONF_OUTPUTS): cv.All(
                cv.ensure_list(cv.use_id(output.FloatOutput)), cv.Length(min=1)
            ),
            cv.Optional(CONF_MIN_BRIGHTNESS, default=0): cv.percentage,
            cv.Optional(CONF_MAX_BRIGHTNESS, default=1): cv.percentage,
        }
    ),
    validate_range,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
    cg.add(var.set_max_brightness(config[CONF_MAX_BRIGHTNESS]))
    cg.add(var.set_min_brightness(config[CONF_MIN_BRIGHTNESS]))
    for output_id in config[CONF_OUTPUTS]:
        out = await cg.get_variable(output_id)
        cg.add(var.add_output(out))
