# Research sniffer (optional)

Stand-alone PlatformIO firmware for a T-Beam v1.2 that was used to reverse engineer the protocol. You don't need
it to use the ESPHome component. It's useful for checking your own system, calibrating `frequency`, or
investigating other Remeha models.

- `src/main.cpp`: SX1276 in continuous FSK mode, RSSI-gated.
  - **Edge mode** (default): raw demodulator edge timings plus a frequency-error estimate per burst.
  - **Bit-sync mode** (`b 32768`): demodulated chips per burst.
  - `x <hex>` transmits **one** packet (8-byte 0xAA preamble + your chips), at most once per second and at most
    14 dBm. Only transmit if your local regulations allow it.
- `log.py <port> <seconds> <name> [commands…]`: resets the board, sends commands, writes `captures/<name>.log`.
- `decode.py captures/<name>.log`: decodes frames (CRC check, OpenTherm ID/value) and prints the
  `display_template` it finds. Run it without arguments for a self-test.

```sh
pio run -t upload --upload-port /dev/ttyUSB0
python3 log.py /dev/ttyUSB0 120 mycapture 'f 868.2925' 'w 200' 'b 32768'
python3 decode.py captures/mycapture.log
```

Serial commands: `t <dBm>` RSSI threshold, `f <MHz>` frequency, `w <kHz>` RX bandwidth, `b <bps>` bit-sync mode,
`e` edge mode, `r` print RSSI, `d <kHz>` TX deviation, `p <dBm>` TX power, `x <hex>` transmit one packet.

Captures contain your kit ID (see ../../docs/pairing.md). `captures/` is git-ignored.
