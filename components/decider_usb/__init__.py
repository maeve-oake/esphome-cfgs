import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import esp32
from esphome.components.esp32 import (
    VARIANT_ESP32P4,
    VARIANT_ESP32S2,
    VARIANT_ESP32S3,
    add_idf_sdkconfig_option,
)
from esphome.const import CONF_ID

AUTO_LOAD = ["tinyusb"]
DEPENDENCIES = ["tinyusb"]

CONF_CHOICE_TYPE = "choice_type"
CONF_ENTRY_ID = "entry_id"
CONF_INITIAL_BOOT_OPTION = "initial_boot_option"

decider_usb_ns = cg.esphome_ns.namespace("decider_usb")
DeciderUsb = decider_usb_ns.class_("DeciderUsb", cg.Component)
SetBootOptionAction = decider_usb_ns.class_("SetBootOptionAction", automation.Action)


def _validate_boot_option(config):
    choice_type = config[CONF_CHOICE_TYPE]
    if (
        isinstance(choice_type, str)
        and choice_type == "entry_id"
        and CONF_ENTRY_ID not in config
    ):
        raise cv.Invalid("entry_id is required when choice_type is 'entry_id'")
    return config


INITIAL_BOOT_OPTION_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_CHOICE_TYPE): cv.string_strict,
            cv.Optional(CONF_ENTRY_ID): cv.string_strict,
        }
    ),
    _validate_boot_option,
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(DeciderUsb),
            cv.Optional(CONF_INITIAL_BOOT_OPTION): INITIAL_BOOT_OPTION_SCHEMA,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    esp32.only_on_variant(
        supported=[VARIANT_ESP32P4, VARIANT_ESP32S2, VARIANT_ESP32S3],
    ),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    add_idf_sdkconfig_option("CONFIG_TINYUSB_MSC_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_TINYUSB_CDC_ENABLED", False)
    add_idf_sdkconfig_option("CONFIG_TINYUSB_MSC_BUFSIZE", 512)

    if CONF_INITIAL_BOOT_OPTION in config:
        initial = config[CONF_INITIAL_BOOT_OPTION]
        choice_type = initial[CONF_CHOICE_TYPE]
        if CONF_ENTRY_ID in initial:
            cg.add(var.set_boot_option(choice_type, initial[CONF_ENTRY_ID]))
        else:
            cg.add(var.set_boot_option(choice_type))


SET_BOOT_OPTION_ACTION_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(CONF_ID): cv.use_id(DeciderUsb),
            cv.Required(CONF_CHOICE_TYPE): cv.templatable(cv.string_strict),
            cv.Optional(CONF_ENTRY_ID): cv.templatable(cv.string_strict),
        }
    ),
    _validate_boot_option,
)


@automation.register_action(
    "decider_usb.set_boot_option",
    SetBootOptionAction,
    SET_BOOT_OPTION_ACTION_SCHEMA,
)
async def decider_usb_set_boot_option_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])

    choice_type = await cg.templatable(config[CONF_CHOICE_TYPE], args, cg.std_string)
    cg.add(var.set_choice_type(choice_type))

    if CONF_ENTRY_ID in config:
        entry_id = await cg.templatable(config[CONF_ENTRY_ID], args, cg.std_string)
        cg.add(var.set_entry_id(entry_id))

    return var
