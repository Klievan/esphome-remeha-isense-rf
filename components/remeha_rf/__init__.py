"""Remeha iBase RF OpenTherm master over 868 MHz (SX1276), replaces the iSense RF display.

Entity keys follow ESPHome's `opentherm` component (t_set, ch_enable, t_boiler, flame_on, ...) so a
SAT-style integration can map them later.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, number, sensor, switch
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)

AUTO_LOAD = ["sensor", "binary_sensor", "switch", "number"]

ns = cg.esphome_ns.namespace("remeha_rf")
RemehaRF = ns.class_("RemehaRF", cg.Component)
RemehaSwitch = ns.class_("RemehaSwitch", switch.Switch)
RemehaNumber = ns.class_("RemehaNumber", number.Number)

TEMP = dict(
    unit_of_measurement=UNIT_CELSIUS,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_TEMPERATURE,
    state_class=STATE_CLASS_MEASUREMENT,
)
SENSORS = {
    "t_boiler": sensor.sensor_schema(**TEMP),
    "t_ret": sensor.sensor_schema(**TEMP),
    "rel_mod_level": sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT, accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT
    ),
}
BINARY_SENSORS = {
    "flame_on": binary_sensor.binary_sensor_schema(),
    "ch_active": binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_RUNNING),
    "dhw_active": binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_RUNNING),
    "fault_indication": binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM),
    "display_active": binary_sensor.binary_sensor_schema(),
}
SWITCHES = {
    "master": switch.switch_schema(RemehaSwitch, default_restore_mode="RESTORE_DEFAULT_OFF"),
    "ch_enable": switch.switch_schema(RemehaSwitch, default_restore_mode="RESTORE_DEFAULT_OFF"),
    "dhw_enable": switch.switch_schema(RemehaSwitch, default_restore_mode="RESTORE_DEFAULT_ON"),
}

def validate_hex(value):
    if any(c not in "0123456789abcdefABCDEF" for c in value):
        raise cv.Invalid("display_template must be hexadecimal (copy it from the device log)")
    return value.upper()


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RemehaRF),
        cv.Optional("frequency", default=868.2925): cv.float_range(min=868.0, max=868.6),
        cv.Optional("tx_power", default=7): cv.int_range(min=2, max=14),  # 868.0-868.6: 25 mW max
        cv.Optional("deviation", default=20.0): cv.float_range(min=5.0, max=60.0),
        cv.Optional("poll_interval", default="4400ms"): cv.All(
            cv.positive_time_period_milliseconds, cv.Range(min=cv.TimePeriod(milliseconds=1000))
        ),
        cv.Optional("failsafe_timeout", default="15min"): cv.positive_time_period_milliseconds,
        cv.Optional("axp2101", default=True): cv.boolean,
        cv.Optional("display_template"): cv.All(cv.string_strict, cv.Length(min=52, max=52), validate_hex),
        cv.Optional("sck_pin", default=5): cv.int_,
        cv.Optional("miso_pin", default=19): cv.int_,
        cv.Optional("mosi_pin", default=27): cv.int_,
        cv.Optional("cs_pin", default=18): cv.int_,
        cv.Optional("rst_pin", default=23): cv.int_,
        **{cv.Optional(k): s for k, s in {**SENSORS, **BINARY_SENSORS, **SWITCHES}.items()},
        cv.Optional("t_set"): number.number_schema(
            RemehaNumber, unit_of_measurement=UNIT_CELSIUS, device_class=DEVICE_CLASS_TEMPERATURE
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_pins(config["sck_pin"], config["miso_pin"], config["mosi_pin"], config["cs_pin"], config["rst_pin"]))
    cg.add(var.set_frequency(config["frequency"]))
    cg.add(var.set_tx_power(config["tx_power"]))
    cg.add(var.set_deviation(config["deviation"]))
    cg.add(var.set_poll_interval(config["poll_interval"]))
    cg.add(var.set_failsafe_timeout(config["failsafe_timeout"]))
    if "display_template" in config:
        cg.add(var.set_display_template(config["display_template"]))
    cg.add_library("SPI", None)
    if config["axp2101"]:
        cg.add_library("Wire", None)
        cg.add_library("lewisxhe/XPowersLib", "0.2.6")
        cg.add_define("REMEHA_RF_AXP2101")

    for key in SENSORS:
        if key in config:
            cg.add(getattr(var, f"set_{key}_sensor")(await sensor.new_sensor(config[key])))
    for key in BINARY_SENSORS:
        if key in config:
            cg.add(getattr(var, f"set_{key}_binary_sensor")(await binary_sensor.new_binary_sensor(config[key])))
    for key in SWITCHES:
        if key in config:
            sw = await switch.new_switch(config[key])
            cg.add(sw.set_parent(var))
            cg.add(getattr(var, f"set_{key}_switch")(sw))
    if "t_set" in config:
        n = await number.new_number(config["t_set"], min_value=10, max_value=80, step=0.5)
        cg.add(n.set_parent(var))
        cg.add(var.set_t_set_number(n))
