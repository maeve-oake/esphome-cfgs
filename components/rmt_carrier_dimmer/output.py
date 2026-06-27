from esphome import pins
import esphome.codegen as cg
from esphome.components import esp32, esp32_rmt, output
from esphome.components.esp32 import include_builtin_idf_component
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PIN

DEPENDENCIES = ["esp32"]

CONF_CARRIER_DUTY = "carrier_duty"
CONF_CARRIER_FREQUENCY = "carrier_frequency"
CONF_ENVELOPE_FREQUENCY = "envelope_frequency"

RMT_RESOLUTION_HZ = 20_000_000
RMT_SYMBOL_DURATION_MAX = 0x7FFF


def validate_carrier_duty(value):
    value = cv.percentage(value)
    return cv.float_range(min=0.0, max=1.0, min_included=False, max_included=False)(
        value
    )


def validate_envelope_frequency(value):
    value = cv.frequency(value)
    value = cv.float_range(min=0.0, min_included=False)(value)
    period_ticks = round(RMT_RESOLUTION_HZ / value)
    if period_ticks > RMT_SYMBOL_DURATION_MAX:
        min_frequency = RMT_RESOLUTION_HZ / RMT_SYMBOL_DURATION_MAX
        raise cv.Invalid(
            f"envelope_frequency is too low for a single-symbol loop at {RMT_RESOLUTION_HZ} Hz resolution; "
            f"use at least {min_frequency:.1f} Hz"
        )
    return value


rmt_carrier_dimmer_ns = cg.esphome_ns.namespace("rmt_carrier_dimmer")
RMTCarrierDimmerOutput = rmt_carrier_dimmer_ns.class_(
    "RMTCarrierDimmerOutput", output.FloatOutput, cg.Component
)

CONFIG_SCHEMA = cv.All(
    esp32.only_on_variant(
        unsupported=list(esp32_rmt.VARIANTS_NO_RMT),
        msg_prefix="RMT carrier dimmer",
    ),
    output.FLOAT_OUTPUT_SCHEMA.extend(
        {
            cv.Required(CONF_ID): cv.declare_id(RMTCarrierDimmerOutput),
            cv.Required(CONF_PIN): pins.internal_gpio_output_pin_schema,
            cv.Optional(CONF_CARRIER_FREQUENCY, default="205kHz"): cv.All(
                cv.frequency, cv.float_range(min=0.0, min_included=False)
            ),
            cv.Optional(CONF_CARRIER_DUTY, default="50%"): validate_carrier_duty,
            cv.Optional(
                CONF_ENVELOPE_FREQUENCY, default="1kHz"
            ): validate_envelope_frequency,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_with_framework("esp-idf"),
)


async def to_code(config):
    include_builtin_idf_component("esp_driver_rmt")

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    var = cg.new_Pvariable(config[CONF_ID], pin)
    await cg.register_component(var, config)
    await output.register_output(var, config)
    cg.add(var.set_carrier_frequency(int(config[CONF_CARRIER_FREQUENCY])))
    cg.add(var.set_carrier_duty(config[CONF_CARRIER_DUTY]))
    cg.add(var.set_envelope_frequency(float(config[CONF_ENVELOPE_FREQUENCY])))
