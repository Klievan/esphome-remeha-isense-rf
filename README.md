# ESPHome Remeha iSense RF

Control a Remeha boiler through its original **iBase RF** receiver from Home Assistant, replacing the **iSense RF**
wireless room display with an ESP32 + SX1276 board running ESPHome.

> **Unofficial, independent project.** Not affiliated with, endorsed by or supported by Remeha / BDR Thermea or the
> OpenTherm Association. The protocol was worked out only from radio captures and observed behaviour. No Remeha
> firmware, source code or confidential documentation was used. Heating control is safety-relevant: read
> [Limitations and safety](#limitations-and-safety) before you use it.

## What this is

A Remeha iSense RF system has two parts:

- **iSense RF**: a battery-powered room display/thermostat. It measures room temperature and decides how hot the
  boiler water should be.
- **iBase RF**: a small receiver wired to the boiler's 2-wire OpenTherm bus. It relays OpenTherm data between the
  boiler and the display over an 868 MHz radio link.

The radio link turned out to carry **plain OpenTherm data IDs and values** in a simple Manchester-coded FSK frame
with a CRC-16 (details: [docs/protocol.md](docs/protocol.md)). This component speaks that protocol, so an ESP32
can take the display's place: it enables heating, writes the boiler flow temperature (OpenTherm control setpoint)
and reads back boiler status, flame, modulation and temperatures. The iBase and the boiler wiring stay untouched,
and you can go back to the original display at any time.

## Status

| Feature | Status |
|---|---|
| Receive and decode display ↔ iBase traffic (listen-only) | ✅ tested |
| Take over as master: CH enable, DHW enable (OpenTherm ID 0 flags) | ✅ tested |
| Write control setpoint (ID 1), boiler regulates to it | ✅ tested (40 °C and 55 °C) |
| Read flame / CH active / DHW active / fault (ID 0), modulation (ID 17), flow temperature (ID 25) | ✅ tested |
| Return temperature (ID 28) | ⚠️ works, but the iBase alternates real values with `0`, which are filtered out |
| Learn the kit's `display_template` from the original display | ✅ tested |
| Ignore frames from other kits (neighbours) | ⚙️ implemented, only tested with frames from a single kit |
| Failsafe: drop CH enable when Home Assistant is gone for `failsafe_timeout` | ⚙️ implemented, not yet exercised on hardware |
| Pause transmitting while the real display is active | ✅ tested |
| Room setpoint / clock writes (IDs 16, 20–22), DHW setpoint, max modulation | ❌ not implemented |
| Room temperature regulation on the device | ❌ by design: done in Home Assistant |
| Other Remeha RF products, other boards (SX1262, AXP192) | ❌ untested / unsupported |

Tested on one installation: one iSense RF + iBase RF kit on a Remeha OpenTherm gas boiler, in Belgium, Home
Assistant 2026.9, ESPHome 2026.6.5.

## Hardware

- **LilyGO T-Beam v1.2, 868 MHz** (ESP32 + Semtech SX1276 + AXP2101). No wiring needed.
- A 5 V USB supply (permanent power).
- A room temperature sensor in Home Assistant (preferably 0.1 °C resolution).

Details, pins, register values and unsupported boards: [docs/hardware.md](docs/hardware.md).

## Installation

Requirements: ESPHome 2026.6 or newer (tested with 2026.6.5), Arduino framework. Dependencies are pulled in
automatically: Arduino `SPI`/`Wire` and [XPowersLib](https://github.com/lewisxhe/XPowersLib) 0.2.6 (MIT).

1. Copy [`examples/lilygo-t-beam-v1.2.yaml`](examples/lilygo-t-beam-v1.2.yaml) and
   [`examples/secrets.yaml.example`](examples/secrets.yaml.example) (as `secrets.yaml`) into your ESPHome config
   folder. Fill in Wi-Fi, an API encryption key and an OTA password. Leave `display_template` commented out.
2. The example pulls the component from `github://Klievan/esphome-remeha-isense-rf`. To use a local checkout
   instead, set `source: {type: local, path: <checkout>/components}`.
3. Flash over USB the first time: `esphome run lilygo-t-beam-v1.2.yaml`. Some T-Beams need 115200 baud for
   flashing; the example sets that.
4. Adopt the device in Home Assistant (Settings → Devices & services → ESPHome). It starts **listen-only**.
5. Learn your kit's template from your display and add it to the config: **[docs/pairing.md](docs/pairing.md)**.
6. Remove the iSense batteries and turn on **RF master**.

### Home Assistant thermostat

The component exposes the boiler. Regulating room temperature is left to Home Assistant. The simplest setup is
on/off control with a fixed flow temperature:

```yaml
# configuration.yaml
climate:
  - platform: generic_thermostat
    name: Living room
    heater: switch.remeha_rf_central_heating_enable
    target_sensor: sensor.living_room_temperature   # your room sensor
    min_temp: 15
    max_temp: 24
    target_temp: 20
    cold_tolerance: 0.3
    hot_tolerance: 0.2
    min_cycle_duration:
      minutes: 10
    initial_hvac_mode: "heat"
```

Set **Control setpoint** to a flow temperature that suits your radiators (we use 55 °C). The value is stored on
the device. Entity keys follow ESPHome's `opentherm` component (`t_set`, `ch_enable`, `t_boiler`, `flame_on`,
…). At the time of writing, the SAT integration's ESPHome mode doesn't find ESPHome entities on Home Assistant
2026.8+ (SAT issue #175), so it hasn't been tested with this component.

Two caveats for `generic_thermostat`:
- With a sensor that only reports whole degrees, any tolerance below 1 °C behaves like ±1 °C.
- If the room sensor becomes unavailable, `generic_thermostat` keeps the heater in its last state. Consider an
  automation that turns the thermostat off when the sensor has been unavailable for a while.

## Entities

| Key | Type | Meaning |
|---|---|---|
| `master` | switch | RF master: when off, listen-only |
| `ch_enable` / `dhw_enable` | switch | OpenTherm master flags sent with every ID 0 poll |
| `t_set` | number (°C) | control setpoint (boiler flow temperature), persisted |
| `t_boiler`, `t_ret` | sensor (°C) | flow / return temperature |
| `rel_mod_level` | sensor (%) | relative modulation |
| `flame_on`, `ch_active`, `dhw_active`, `fault_indication` | binary sensor | OpenTherm slave status (ID 0) |
| `display_active` | binary sensor | the original iSense was heard in the last 120 s |

Options: `display_template`, `frequency` (868.2925), `tx_power` (7 dBm, max 14), `deviation` (20 kHz),
`poll_interval` (4.4 s, min 1 s), `failsafe_timeout` (15 min, 0 = off), `axp2101` (true), `sck_pin`, `miso_pin`,
`mosi_pin`, `cs_pin`, `rst_pin`.

## Verifying communication

Expected log output once the template is set and RF master is on. The values are examples:

```
[C][remeha_rf]: Remeha iBase RF master (SX1276)
[C][remeha_rf]:   Frequency: 868.2925 MHz, TX 7 dBm, deviation 20.0 kHz
[C][remeha_rf]:   Poll interval: 4400 ms, failsafe: 900 s
[C][remeha_rf]:   Display template: configured
[D][remeha_rf]: TX READ id=0 val=0x0300 seq=2
[D][remeha_rf]: RX base type=24 id=0 val=0x030A (3.04)      <- CH active + flame
[D][remeha_rf]: TX WRITE id=1 val=0x3700 seq=3
[D][remeha_rf]: RX base type=24 id=1 val=0x3700 (55.00)     <- setpoint echoed
[D][remeha_rf]: TX READ id=25 val=0x0000 seq=4
[D][remeha_rf]: RX base type=24 id=25 val=0x3AE6 (58.90)    <- flow temperature
[D][remeha_rf]: RX last 60 s: base=13 display=0 bad=0 other-kit=0
```

Every `TX` should be followed by an `RX base` line about 125 ms later.

## Troubleshooting

| Symptom | Check |
|---|---|
| `SX127x not found (RegVersion=0x..)` | Wrong board/pins; on T-Beam v1.0/1.1 the radio isn't powered by `axp2101` |
| Listen-only: no `RX` lines at all | Display asleep or in a menu (it may poll only every ~50 s); move closer; is it the 868 MHz board? |
| `RX last 60 s: … bad=N` growing | Weak signal or interference: move the board, check the antenna |
| No display template line | Only frames *from the display* produce it; change the setpoint on the display to trigger traffic |
| RF master on, `TX` lines but no `RX base` | Template from a different kit; frequency offset (see hardware.md, *Frequency calibration*); distance to the iBase |
| RF master on, no `TX` lines | `display_template` missing (error in log), or the real display was heard in the last 120 s |
| `other-kit=` counts but no `RX` | Frames from a neighbour's system are being filtered; check your template |
| Return temperature drops to 0 | Fixed: zero readings are filtered |

## Limitations and safety

- **This controls a gas boiler.** The boiler's own safety functions (overheat, flow limits, frost protection)
  remain active, but faulty control can still mean an overheated or cold house. Monitor it, especially at first.
- **Communication loss**:
  - Home Assistant unreachable for `failsafe_timeout` (15 min): the device switches **CH enable off** and keeps
    polling (DHW stays enabled). Short restarts don't interrupt heating.
  - Device off, crashed or out of range: no RF master. What the iBase/boiler then does is **not known** (not
    tested). Keep the original display available as a fallback.
  - Room sensor unavailable: handled in Home Assistant, not on the device (see the thermostat caveats above).
- **Collisions**: never run RF master while the original display is active. The component pauses for 120 s
  whenever it hears the display, but two masters would still interfere briefly.
- **Security of the RF protocol** (factual, from our captures, see [protocol.md §8](docs/protocol.md#8-security-properties-factual)):
  no encryption, no authentication beyond a kit ID broadcast in every frame, no replay protection. Anyone in radio
  range can read the boiler data and could send commands the iBase accepts. That's a property of the original
  product, not something this project adds; this project makes it documented.
- **Radio regulations**: you're responsible for complying with local rules (frequency, power, duty cycle). The
  defaults use ~0.4 % duty cycle at 7 dBm in the 868.0–868.6 MHz sub-band. See
  [hardware.md](docs/hardware.md#radio-regulations).

## Development

- `components/remeha_rf/remeha_frame.h`: protocol codec (no ESPHome dependencies).
- `components/remeha_rf/remeha_rf.*`: ESPHome component (SX1276 driver, polling, entities).
- Unit tests: `c++ -std=c++17 -Wall -Wextra -o /tmp/t tests/test_remeha_frame.cpp && /tmp/t`
- `tools/sniffer/`: the research sniffer firmware and Python decoder used to reverse engineer the protocol.
- CI (`.github/workflows/ci.yml`) runs the unit tests, the decoder self-test and an ESPHome compile of the example.

Contributions are welcome, especially captures from other Remeha RF products; see
[CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT, see [LICENSE](LICENSE). ESPHome itself is MIT (Python) / GPLv3 (C++ runtime). A compiled firmware image that
contains ESPHome's C++ core is therefore subject to GPLv3 when distributed. "Remeha", "iSense", "iBase" and
"OpenTherm" are trademarks of their respective owners and are used here only to describe compatibility.
