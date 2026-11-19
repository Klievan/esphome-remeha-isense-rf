# Hardware

## Tested board

Only this combination has been tested:

| Item | Details |
|---|---|
| Board | **LilyGO T-Beam v1.2**, 868 MHz version |
| MCU | ESP32-D0WDQ6-V3 (rev 3.1), 4 MB flash |
| Radio | Semtech **SX1276** (868/915 MHz) on SPI; read back `RegVersion = 0x12` |
| Power management | X-Powers **AXP2101**. The radio's 3.3 V rail is ALDO2, which the component switches on at boot |
| USB-serial | WCH CH9102 (corrupted transfers above 115200 baud on macOS during testing) |
| Antenna | the 868 MHz antenna supplied with the board |

No wiring is needed: the radio is on the board. The GPS and the 18650 holder aren't used.

### Pins used (T-Beam v1.2 defaults, configurable)

| Signal | GPIO |
|---|---|
| SPI SCK / MISO / MOSI | 5 / 19 / 27 |
| SX1276 NSS (CS) | 18 |
| SX1276 RESET | 23 |
| AXP2101 I²C SDA / SCL | 21 / 22 (fixed in code) |

DIO pins aren't used: the component polls the radio's IRQ flags over SPI.

### Other boards

- **SX1278 (433 MHz) T-Beams** report the same `RegVersion` but have a 433 MHz RF front end, so they won't work
  at 868 MHz. Check that your board is the 868 MHz variant.
- **T-Beam v1.0/v1.1 (AXP192)**: untested. The radio power rail is on a different PMU, so the `axp2101` option
  doesn't apply.
- **SX1262 boards** (for example Heltec WiFi LoRa 32 V3, T-Beam Supreme): **not supported**. They use a different
  radio with a different register interface.
- Any ESP32 with an SX1276 on SPI should work in principle with `axp2101: false` and the right pins, but that's
  untested.

## Bill of materials

| Qty | Part | Notes |
|---|---|---|
| 1 | LilyGO T-Beam v1.2, **868 MHz** | includes antenna |
| 1 | USB cable + 5 V USB power supply | the device must be powered permanently |
| 1 | Home Assistant installation with the ESPHome integration | for control |
| 1 | Room temperature sensor in Home Assistant | the iSense measured room temperature itself; you need a replacement. Prefer 0.1 °C resolution |
| — | (optional) a second SX1276 board or an SDR | only for protocol research |

## Radio configuration

The component configures the SX1276 in FSK packet mode with these register values:

| Register | Addr | RX | TX | Purpose |
|---|---|---|---|---|
| RegOpMode | 0x01 | 0x05 | 0x03 | FSK, high-frequency band, RX/TX |
| RegBitrate MSB/LSB + Frac | 0x02/0x03, 0x5D | 0x03 0xD0, 9 | same | 32 MHz / (976 + 9/16) = 32 768 chip/s |
| RegFdev | 0x04/0x05 | — | 0x0147 | 20 kHz (`deviation`) |
| RegFrf | 0x06–0x08 | 0xD912B7 | same | 868.2925 MHz (`frequency`) |
| RegPaConfig | 0x09 | — | 0x80 + (P − 2) | PA_BOOST, P = `tx_power` dBm (7 by default) |
| RegPaRamp | 0x0A | — | 0x09 | no shaping, 40 µs ramp |
| RegLna | 0x0C | 0x23 | — | max gain, HF boost |
| RegRxConfig | 0x0D | 0x08 (+0x40 restart strobe) | — | AGC on, AFC off |
| RegRxBw | 0x12 | 0x09 | — | 200 kHz (the iBase sits ~11 kHz above the display) |
| RegOokPeak | 0x14 | 0x28 | — | bit synchroniser on |
| RegPreambleDetect | 0x1F | 0x8A | — | on, 1 byte, tolerance 10 |
| RegPreamble | 0x25/0x26 | — | 0x0008 | 8 bytes `0xAA` |
| RegSyncConfig | 0x27 | 0x52 | 0x00 | RX: auto-restart, sync on, 3 bytes; TX: sync off (sync chips are in the payload) |
| RegSyncValue1..3 | 0x28–0x2A | AA AA A3 | — | kit-independent sync (see protocol.md §3) |
| RegPacketConfig1 | 0x30 | 0x00 | 0x00 | fixed length, no DC-free, no hardware CRC |
| RegPacketConfig2 | 0x31 | 0x40 | 0x40 | packet mode |
| RegPayloadLength | 0x32 | 56 | 58 | |
| RegFifoThresh | 0x35 | — | 0x8F | start TX when the FIFO isn't empty |

Manchester coding and the CRC are done in software (`remeha_frame.h`), not by the radio.

### Frequency calibration

`frequency: 868.2925` is where the original display measured ~0 Hz frequency error on *this* T-Beam's crystal.
Other boards' crystals differ by a few ppm (a few kHz at 868 MHz). The 200 kHz receive bandwidth tolerates that,
and so far the iBase accepted our transmissions on the first try. If your iBase doesn't answer, run the sniffer
(`tools/sniffer`, see its README) in its default edge mode. Each `BURST` line includes a frequency-error estimate
(`fei=`). It's noisy, so average several of the display's frames and adjust `frequency` by that amount. (The
estimate isn't usable in the sniffer's bit-sync mode.)

## Radio regulations

You're responsible for complying with your local radio regulations. In the EU, the 868.0–868.6 MHz
short-range-device sub-band generally allows up to 25 mW ERP with a 1 % duty cycle (check the rules that apply
where you live, e.g. ERC Recommendation 70-03 and national implementations). With the defaults (7 dBm conducted,
one ≈16.5 ms transmission every 4.4 s) the duty cycle is about 0.4 %. The component caps `tx_power` at 14 dBm and
enforces at least 1 s between transmissions. Don't raise power or poll faster than the rules allow.
