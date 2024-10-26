import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import spi
from esphome.components import display
from esphome import pins
from esphome.const import (
    CONF_ID,
    CONF_LAMBDA,
)
CONF_RESET_PIN = 'reset_pin'
CONF_EN_PIN = 'en_pin'
CONF_INTENSITY = 'intensity'
CONF_DIGITS = 'digits'
CONF_SCROLL_DELAY = 'scroll_delay'
CONF_REMOVE_SPACES = 'remove_spaces'
CONF_REPLACE_STRING = 'replace'
CONF_CUSTOM_STRING = 'custom'

CODEOWNERS = ["@Trip5"]

DEPENDENCIES = ["spi"]

vfd8md06inkm_ns = cg.esphome_ns.namespace("vfd8md06inkm")
VFDComponent = vfd8md06inkm_ns.class_(
    "VFDComponent", cg.PollingComponent, spi.SPIDevice
)
VFDComponentRef = VFDComponent.operator("ref")

def validate_scroll_delay(scroll_delay):
    try:
        parts = scroll_delay.split(',')
        if len(parts) != 2: raise cv.Invalid("Must provide two comma-separated values")
        initial_delay = int(parts[0].strip())
        subsequent_delay = int(parts[1].strip())
        if not (100 <= initial_delay <= 10000): raise cv.Invalid("Initial delay must be between 100 and 10000 milliseconds")
        if not (100 <= subsequent_delay <= 10000): raise cv.Invalid("Subsequent delay must be between 100 and 10000 milliseconds")
        return initial_delay, subsequent_delay
    except ValueError:
        raise cv.Invalid("Scroll delay must be two integers separated by a comma (100 to 10000 milliseconds)")

CONFIG_SCHEMA = (
    display.BASIC_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(VFDComponent),
            cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_EN_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_INTENSITY, default=200): cv.int_range(min=0, max=255),
            cv.Optional(CONF_DIGITS, default=8): cv.int_range(6,8,16),
            cv.Optional(CONF_SCROLL_DELAY, default="2000,500"): cv.All(cv.string, validate_scroll_delay),
            cv.Optional(CONF_REMOVE_SPACES, default=False): cv.boolean,
            cv.Optional(CONF_REPLACE_STRING, default=""): cv.string,
            cv.Optional(CONF_CUSTOM_STRING, default=""): cv.string,
        }
    )
    .extend(cv.polling_component_schema("1s"))
    .extend(spi.spi_device_schema())
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await spi.register_spi_device(var, config)
    await display.register_display(var, config)

    if CONF_RESET_PIN in config:
        reset_pin = await cg.gpio_pin_expression(config[CONF_RESET_PIN])
        gpio_number = reset_pin.get_pin()
        cg.add(var.set_reset_pin(gpio_number))
    else:
        cg.add(var.set_reset_pin(-1))

    if CONF_EN_PIN in config:
        en_pin = await cg.gpio_pin_expression(config[CONF_EN_PIN])
        gpio_number = en_pin.get_pin()
        cg.add(var.set_en_pin(gpio_number))
    else:
        cg.add(var.set_en_pin(-1))

    cg.add(var.intensity(config[CONF_INTENSITY]))
    cg.add(var.set_digits(config[CONF_DIGITS]))

    if CONF_SCROLL_DELAY in config:
        initial_delay, subsequent_delay_value = config[CONF_SCROLL_DELAY]
        cg.add(var.set_scroll_delays(initial_delay, subsequent_delay_value))

    cg.add(var.set_remove_spaces(config[CONF_REMOVE_SPACES]))
    cg.add(var.set_replace_string(config[CONF_REPLACE_STRING]))
    cg.add(var.set_custom_string(config[CONF_CUSTOM_STRING]))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(VFDComponentRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))
