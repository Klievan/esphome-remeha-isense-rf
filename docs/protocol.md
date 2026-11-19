# Remeha iSense RF ↔ iBase RF radio protocol

This describes the 868 MHz link between a Remeha **iSense RF** room unit (battery display) and the **iBase RF**
receiver that connects to the boiler's OpenTherm bus. It was worked out **only from RF captures and from observing
how the system behaved**, using a LilyGO T-Beam (SX1276) as receiver and later as transmitter. No firmware was
read, extracted or disassembled, and no Remeha documentation beyond the public user manual was used.

All observations come from **one installation** (one paired display + iBase kit, a Remeha boiler with OpenTherm).

## Evidence levels

Every statement below carries one of these tags:

| Tag | Meaning |
|---|---|
| **[C]** confirmed by captures | Seen consistently in recorded traffic between the original devices (217 frames, both directions) |
| **[E]** experimentally verified | We transmitted frames and observed the iBase/boiler respond as described |
| **[I]** inferred | Our best interpretation, consistent with the data but not proven |
| **[U]** unknown | Not determined |

## 1. System roles

- **iBase is a pure responder.** It never transmitted while the display was unpowered (several minutes of
  silence) **[C]**. It answers every request from the display **[C]** and from our transmitter **[E]**.
- **The iSense display holds the thermostat logic.** It measures room temperature locally, and room temperature
  (OpenTherm ID 24) was never sent over RF **[C]**. It writes a **control setpoint** (boiler flow temperature,
  ID 1) and the master enable flags (ID 0) **[C]**. So the iBase acts as an RF ↔ OpenTherm bridge **[I]**.
- The iBase's radio is a TI CC1101 (visual identification of the chip) **[C]**. The display's radio is **[U]**.

## 2. Physical layer

| Parameter | Value | Evidence |
|---|---|---|
| Band | 868 MHz SRD; the user manual states 868.3 MHz | manual |
| Carrier as measured by our SX1276 | display ≈ 868.2925 MHz (frequency-error indicator ≈ 0 there); iBase ≈ 11 kHz higher | **[C]** (FEI readings are noisy); absolute values depend on our crystal **[I]** |
| Modulation | 2-FSK | **[C]** |
| Deviation | **[U]** for the original devices; 20 kHz from our transmitter is accepted | **[E]** |
| Pulse shaping | **[U]** (we transmit without shaping, accepted) | **[E]** |
| Chip rate | 32 768 chip/s (measured 32.74 kchip/s over whole packets) | **[C]**, **[E]** |
| Line code | Manchester: chip pair `01` = bit 1, `10` = bit 0 (as seen on the SX1276 data output) | **[C]** |
| Net bit rate | 16 384 bit/s | follows from above |
| Packet airtime | ≈ 15.5 ms (≈ 16.5 ms with our 8-byte preamble) | **[C]**, **[E]** |

Note on polarity: "01 = 1" is relative to the SX1276's demodulated output. Which RF tone that corresponds to was
not separately verified **[U]**. It doesn't matter as long as the same radio receives and transmits.

## 3. Framing

```
 ...1010101010 | 0 0 1 1 1 | Manchester( 206 data bits | 16 CRC bits ) | (display only: ~4 junk bits)
   preamble       sync tail       = 444 chips
```

- **Preamble**: alternating `1010…`. At least 39 chips were visible in every RSSI-gated capture **[C]**, so the
  real length is longer. 64 chips (8 × `0xAA`) from our transmitter are accepted **[E]**.
- **Sync**: the preamble's last `0` followed by `00111`, so `000111`, a deliberate Manchester violation **[C]**.
- **Receiver sync word used by this project**: `AA AA A3` (24 chips: preamble tail + `0011`). It is
  **kit-independent** and matched all 217 captured frames **[C]**. The payload then starts with the final `1` of
  `00111`, followed by the Manchester chips.
- **Data**: 206 bits, MSB-first as transmitted **[C]**. They're numbered 0..205 below.
- **CRC-16** over the 206 data bits, appended MSB-first **[C]**:
  - polynomial `0x3D65` (the polynomial of EN 13757 / CRC-16/DNP)
  - bitwise, MSB-first, register initialised to `0x1A12` before the first data bit, no final XOR
  - verified on 217/217 captured frames in both directions **[C]**; frames we generate with it are accepted
    **[E]**
  - `0x1A12` is an *effective* initial value; whether it corresponds to a standard parameter set is **[U]**. It
    was derived from one kit **[I: likely the same for all kits]**.
- **Trailing bits**: the display sends about 4 more Manchester bits after the CRC, with random-looking values
  that aren't covered by the CRC **[C]**. The iBase sends none **[C]**. They look like transmitter turn-off
  junk **[I]**.
- There is no length field. All observed frames are the same length **[C]**.

## 4. Field map (data bits 0..205)

| Bits | Field | Values / notes | Evidence |
|---|---|---|---|
| 0–123 | **Kit ID** | Constant in every frame of one kit, in both directions, and unchanged across a display power cycle **[C]**. The hex rendering contains fragments of the display's printed serial number, and the iBase's label code appears verbatim **[C]**. Factory-programmed pairing identity **[I]** | **[C]/[I]** |
| 124–125 | Source | display `01`, iBase `10` | **[C]** values, **[I]** meaning |
| 126–139 | — | constant, meaning **[U]** | **[C]** constant |
| 140–141 | Destination | display→iBase `10`, iBase→display `01` | **[C]** values, **[I]** meaning |
| 142–145 | — | constant, meaning **[U]** | **[C]** constant |
| 146–148 | Sequence | 3-bit, changes between frames; the pattern isn't simply +1 (replies sometimes repeat the request's value, sometimes jump) **[C]**. **The iBase does not check it**: replayed and synthesised frames with any value are answered **[E]** | **[C]/[E]** |
| 149–159 | — | constant, meaning **[U]** | **[C]** constant |
| 160–167 | Message type | `0x1C` display write, `0x20` display read, `0x24` iBase reply | **[C]** values, **[I]** names (from behaviour) |
| 168–171 | — | constant, meaning **[U]** | **[C]** constant |
| 172–181 | OpenTherm data ID | 10-bit field = **data ID + 0x13C** (e.g. ID 0 → `0x13C`, ID 126 → `0x1BA`). Why the offset exists is **[U]** | **[C]** for every ID in §6 |
| 182–189 | Direction | `0x01` from display, `0x00` from iBase | **[C]** |
| 190–205 | OpenTherm data value | 16-bit, OpenTherm encoding (f8.8 for temperatures) | **[C]**: setpoint steps on the display's UI appeared exactly as f8.8 values |
| 206–221 | CRC-16 | see §3 | **[C]** |

The OpenTherm wire frame's own parity and message-type bits don't appear as such; the RF message type takes their
place **[I]**.

## 5. Message exchange

- **Request/response**: the display sends one request, and the iBase replies about **123–125 ms** after the
  request ends **[C]**. Our transmitter sees the same timing **[E]**.
- **Batched replies**: one request can be answered by **several iBase frames** about 165–190 ms apart, each
  carrying a different data ID **[C]**. Seen after a display power-up (IDs 16, 1, 2, 3, 6, 10, 14, 20, 21, 22, 48,
  49, 56, 57, 109, 110, 127) and after date/time changes. They look like a cached data sync **[I]**.
- **Read** (`0x20`): value is `0x0000`, **except ID 0**, whose high byte carries the master status flags
  (bit 0 CH enable, bit 1 DHW enable, as in OpenTherm) **[C]**. Changing those flags changes the boiler's
  behaviour **[E]**. So an ID 0 "read" is a command too.
- **Write** (`0x1C`): the iBase echoes the written value in its reply **[C]**, **[E]**.
- **Reply** (`0x24`): carries the OpenTherm data. For ID 0 the low byte is the slave status: bit 0 fault,
  bit 1 CH active, bit 2 DHW active, bit 3 flame **[C]** (matched the display's flame/radiator icons and the
  boiler's behaviour).
- **Acknowledgement / retransmission**: there's no separate ACK frame; the reply is the acknowledgement **[I]**.
  Whether the display retransmits when no reply comes is **[U]**. This project doesn't retransmit and just moves
  on to the next poll.
- **Display polling rhythm**: in normal operation the display used one exchange per **≈4.43 s** slot, but not
  every slot **[C]**. After a power-up or in its menus it polled much less often (every ~50 s) **[C]**.
- **iBase timeout**: what the iBase or boiler does when no RF master is heard for a long time is **[U]**. It
  wasn't tested. This project polls every 4.4 s.

### Exchange example (behaviour, values from a real session, kit bits omitted)

```
display → READ  id=0   val=0x0300   (master: CH enable + DHW enable)
iBase   → REPLY id=0   val=0x030A   (slave: CH active + flame on)
display → WRITE id=1   val=0x3700   (control setpoint 55.0 °C)
iBase   → REPLY id=1   val=0x3700   (echo)
display → READ  id=25  val=0x0000
iBase   → REPLY id=25  val=0x3AE6   (flow temperature 58.90 °C)
```

## 6. OpenTherm data IDs seen on the link

| ID | OpenTherm meaning | Direction seen | Notes |
|---|---|---|---|
| 0 | Status | read (flags) / reply | see §5 **[C]**, **[E]** |
| 1 | Control setpoint | write | display wrote ~60 °C; 40 and 55 °C from our transmitter were honoured by the boiler (flow temperature settled at the written value) **[E]** |
| 2 / 3 | Master / slave configuration | reply (batch) | **[C]** |
| 6 | Remote boiler parameter flags | reply (batch) | **[C]** |
| 10 | Number of TSPs | read / reply | **[C]** |
| 14 | Max relative modulation | reply (batch) | **[C]** |
| 16 | Room setpoint | write | the display's user setpoint, informational **[C]** |
| 17 | Relative modulation level | read / reply | **[C]** |
| 20 / 21 / 22 | Day+time / date / year | write | matched the display's clock settings exactly **[C]** |
| 25 | Boiler flow temperature | read / reply | **[C]** |
| 28 | Return water temperature | read / reply | replies alternate between a plausible value and `0x0000` **[C]**; we treat `0` as invalid **[I]** |
| 48 / 49 | DHW / max-CH setpoint bounds | reply (batch) | **[C]** |
| 56 / 57 | DHW setpoint / max CH setpoint | reply (batch) | **[C]** |
| 109 / 110 | — | read / reply | meaning on this system **[U]** |
| 126 / 127 | Master / slave product version | write / reply | **[C]** |

## 7. Pairing

- **No pairing exchange was observed.** After a display power cycle it resumed communicating immediately and only
  asked the user for the date and time **[C]**.
- The kit ID (bits 0–123) is identical before and after **[C]**. The display and iBase carry matching serial
  fragments, and the display PCB has programming pads, so we believe kits are paired at the factory **[I]**.
- Whether a user-initiated re-pairing procedure exists, and what it looks like on air, is **[U]**.
- This project therefore **copies the kit ID from a frame sent by your own display** (see `pairing.md`). Frames
  built that way are accepted by the iBase **[E]**.

## 8. Security properties (factual)

- **No encryption**: all OpenTherm values were readable in plaintext **[C]**.
- **No authentication beyond the kit ID**, which every frame broadcasts in the clear **[C]**. The CRC protects
  against corruption only.
- **No replay protection**: replayed frames were answered, and the sequence field isn't checked **[E]**.
- Consequence: anyone within radio range who captures one frame can read this boiler data and send commands that
  the iBase will accept. The boiler's own safety limits (maximum flow temperature, overheat protection) still
  apply **[I]**.
- The manual's "secure communication" claim isn't reflected in any observed cryptographic mechanism. It may refer
  to the kit ID and the CRC **[I]**.

## 9. Synthetic example frames

Generated with a **random, non-existent kit ID**, so they won't work on a real system. They're also used in
`tests/test_remeha_frame.cpp`.

```
display_template : 9D2E09896916C76AABFF9CF1270AE436494B87C6009000040000

display WRITE id=1 value=0x3700 (55.0 °C) seq=3
  data+CRC (222 bits, hex, 2 zero pad bits): 9D2E09896916C76AABFF9CF1270AE436494B9FC61C94F404DC03CF54
  CRC-16: F3D5

iBase REPLY id=0 value=0x030A seq=4
  data+CRC: 9D2E09896916C76AABFF9CF1270AE43A4947A7C62494F0000C2B1680
  CRC-16: C5A0
```
