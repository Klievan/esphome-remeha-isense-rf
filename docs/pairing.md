# Pairing: getting your kit's display template

## Background

The iSense display and the iBase don't exchange pairing codes over the air. As far as we can tell, a display and
an iBase are paired at the factory, and every frame carries a constant 124-bit **kit ID** (see
`protocol.md` §4 and §7). The iBase accepts frames that carry its kit ID and a valid CRC.

To talk to your iBase, this component sends frames with **the same header your display uses**. It learns that
header, the `display_template`, by listening to your own display. You need a **working iSense display** for this
step. Even a partly broken one is fine, as long as it still talks to the iBase.

> If your display is completely dead, there's currently no known way to obtain the template. Other ways (e.g.
> deriving the kit ID from the serial numbers printed on the devices) haven't been worked out.

## Procedure

1. **Flash listen-only.** Use `examples/lilygo-t-beam-v1.2.yaml` with `display_template` commented out. Leave
   **RF master** off; it's off by default.
2. **Keep the display running** and place the T-Beam within a few metres of it.
3. **Watch the device log** (ESPHome dashboard → Logs, or `esphome logs <your.yaml>`). When the display next
   talks to the iBase (within a minute, or change the setpoint to trigger it), you'll see:
   ```
   [D][remeha_rf]: RX display type=20 id=0 val=0x0300 (3.00)
   [W][remeha_rf]: Heard an iSense display. Put this in your config to enable RF master:
   [W][remeha_rf]:   display_template: "9D2E09896916C76AABFF9CF1270AE436494B87C6009000040000"
   [D][remeha_rf]: RX base type=24 id=0 val=0x030A (3.04)
   ```
   (Example value from a synthetic kit; yours will differ.)
4. **Store it** in `secrets.yaml` as `remeha_display_template: "…"` and enable the line
   `display_template: !secret remeha_display_template`. Flash again.
5. **Check the boot log** for `Display template: configured`. From now on the component also **ignores frames
   from other kits** (e.g. a neighbour's), counted as `other-kit=` in the 60 s statistics line.
6. **Take over**: remove the iSense batteries, then turn **RF master** on. The component waits until it hasn't
   heard the display for 120 s before its first transmission. Within ~10 s after that you should see
   `TX …` / `RX base …` pairs every 4.4 s. If you don't, see the troubleshooting section in the README.

## Going back to the original display

Turn **RF master** off and put the batteries back in the display. It resumes on its own; on our system it asked
for the date and time after the power cycle and had reset the setpoint to its default.

## Privacy

The template contains your kit ID. Anyone within radio range can already receive it (it's in every frame), but
there's no reason to publish it: keep it in `secrets.yaml`, and mask bits 0–123 in captures you share.
