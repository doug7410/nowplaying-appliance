# panelboard

A minimal board whose only job is driving a HUB75 LED matrix.

No mic, no knobs, no display — 5 V in, a HUB75 connector out, and a plug-in
ESP32 dev board on top. It exists because the Now-Playing appliance's Board 1
does HUB75 rendering buried under a lot of other hardware, and it would be nice
to have the panel half on its own board.

- **MCU:** a stock **ESP32-WROOM-32 38-pin dev board** (the narrow Type-C kind,
  DORHEA/HiLetgo/DevKitC-32 — classic ESP32, *not* an S3), dropped into two 1×19
  female headers. USB, RESET/BOOT and the 3.3 V regulator all come with the
  module, so this board carries none of them.
- **Row spacing is 22.86 mm (0.9").** That is the narrow version, and it is
  measured off the board in hand — a 1.0" wide-version clone is one 0.1" grid
  step wider and **will not seat**. Check yours before ordering.
- **Panel drive:** straight 3.3 V logic, no level shifters. That's what Board 1
  does today and it is hardware-verified on a 64×32 panel.
- **Power:** 5 V in on a screw terminal → AOD403 P-FET reverse-polarity
  protection → a second screw terminal that feeds the panel. Panels pull 2–4 A,
  so the 5 V path is 2.5 mm wide throughout.
- 70 × 72 mm, 2 layers, ground pour on both.

## The pin map is the firmware's, not this repo's

The HUB75 GPIO assignment is **WLED-MM's classic-ESP32 default** — the `#else`
branch at the bottom of `wled00/bus_manager.cpp`, which is the ESP32-Trinity /
mrcodetastic map. Wiring to it means the firmware needs no pinout patch at all.
Don't "improve" it here; change it there and follow.

| HUB75 | GPIO | col | | HUB75 | GPIO | col |
|---|---|---|---|---|---|---|
| R1 | 25 | J1 | | A | 23 | J3 |
| G1 | 26 | J1 | | B | 19 | J3 |
| B1 | 27 | J1 | | C | 5 | J3 |
| R2 | 14 | J1 | | D | 17 | J3 |
| G2 | **12** | J1 | | E | 18 | J3 |
| B2 | 13 | J1 | | LAT | 4 | J3 |
| | | | | OE | 15 | J3 |
| | | | | CLK | 16 | J3 |

**All six RGB pins live in the module's J1 column and all eight control pins in
J3.** That is the single fact the whole layout is built on — see below.

**G2 is on IO12, which is the MTDI strapping pin.** It has to read low at reset
or the chip comes up expecting 1.8 V flash, and the panel's ribbon is the only
other thing on that net. `RPD` (10 k to GND) holds it down rather than trusting
the internal pull-down. It lives under the module: lying flat it is ~2.5 mm tall
against ~8.5 mm of socket clearance.

**E is wired**, to IO18. The 64×32 panel doesn't use it (HUB75 pin 8 is ground
there), but the firmware default already says `e = 18`, so a 64×64 panel is a
config change rather than a respin.

GPIO 6–11 are the SPI flash on a classic ESP32 and are left unconnected, as are
IO0/IO2 (strapping), TX0/RX0, and the input-only pins 34–39.

## Firmware: this is not a drop-in for Board 1's build

A plain ESP-WROOM-32 module is **4 MB flash and no PSRAM**. Board 1's WLED-MM
build (`~/Code/WLED-MM` `platformio_override.ini`, `[env:board1]`) pins
`extreme_partitions` — a 3.2 MB app plus a 9 MB filesystem on 16 MB flash — and
that partition table cannot fit here. Build a stock classic-ESP32 WLED-MM target
for this board. A 64×32 DMA framebuffer fits in internal RAM comfortably; 64×64
at high bit depth will not.

## JP1 — pull the shunt before plugging in USB

A dev board's 5 V pin has no reverse-current protection. Leave it tied to a live
panel supply with a USB cable also plugged in and the supply back-feeds the host
port. `JP1` sits in series with the module's 5 V pin so you can break that path;
it is west of the module body so the shunt has headroom.

## Building it

Everything in `board/` is generated. The `.kicad_sch` and `.kicad_pcb` are
committed artifacts, but the generators are the source of truth — edit those.

```
cd board
make all       # regenerate the schematic and the board from generator/*.py
make check     # ERC + DRC with schematic parity, gating on every severity
make zip       # -> panelboard-jlc.zip, ready to upload to jlcpcb.com
make render    # top.png / bottom.png
```

`gen_sch.py` is deterministic (uuid5-derived), so it re-emits the schematic
byte-for-byte. `gen_pcb.py` is not — pcbnew mints fresh UUIDs on every run, so
`make all` rewrites the whole `.kicad_pcb` even when nothing changed. Regenerate
when the design actually changes, not as a habit.

The board is clean at `--severity-all`: 0 errors, 0 warnings, 0 unconnected,
0 parity issues. There is no expected-warning exception to remember.

## Assembly

**Order it as a bare PCB, not PCBA.** Q1 (AOD403, TO-252) is the only surface
mount part on the board and it hand-solders easily; everything else is through
hole. Paying JLC's assembly setup for one FET is not worth it.

| Ref | Part |
|---|---|
| J_ESP_L, J_ESP_R | 1×19 female header, 2.54 mm |
| J_HUB | 2×08 shrouded IDC header, vertical |
| J_PWR, J_PANEL | Phoenix MSTBVA 2,5/2-G, 5.00 mm |
| JP1 | 1×02 male header + shunt |
| Q1 | AOD403 P-FET, TO-252 |
| RG / RPD | 100 k / 10 k axial |
| CB1 / CB2 | 470 µF 16 V radial D8 / 100 nF disc |

Solder `RPD` before the sockets go in — once the module is plugged in you can't
reach it.

## Layout notes worth knowing before you move anything

- **The HUB75 IDC's pin-1 y is 32.43, not a round number.** That puts every IDC
  row exactly on a *gap* between two J1 socket pads, so each even-column control
  signal crosses the pad wall dead straight — no jog, no via, no chance of
  crossing a neighbour. Move the connector off that grid and eight nets get
  much harder.
- **The control bundle puts every horizontal on B.Cu and every vertical on
  F.Cu.** Two horizontals at different y can't collide and neither can two
  verticals at different x, so the eight nets are collision-proof by
  construction — which matters, because the source order and the destination
  order genuinely disagree (E and B invert, and the four odd-column pins have to
  wrap around the west side of the connector). Costs two vias per net in a
  corridor with room to spare.
- **The corridor between the two socket rows is empty on both layers.** That
  free 22.86 mm channel is the one real thing a socketed module buys you over a
  soldered-down WROOM, and the control fan spends all of it.
- **The board's north edge is at y = 18** so the module's antenna end overhangs
  into free air. The antenna straddles pin 1 — about 3 mm either side — so there
  is no way to socket pin 1 *and* keep the board out from under it. Ending the
  board short is cheaper and better than a copper keepout.
- **Zone clearance is 0.25 mm, not 0.30.** At 0.30 the pour cannot squeeze
  between two 2.54 mm-pitch through-hole pads (2.54 − 2×1.15 = 0.24, under the
  0.25 minimum thickness), the socket rows fence it off, and GND pads end up
  stranded on islands. At 0.25 the gap is 0.34 and it flows. JLC's floor for
  2 layers is 0.2, so this still has margin.
- **Both electrolytics stay south of y = 73.8.** The module's USB port overhangs
  its south end at about 10 mm height and a D8 radial cap is ~11 mm tall — put
  one further north and the cable has nowhere to go.
- The IDC's **shroud courtyard is much larger than its pads** (10 × 29 mm). It
  is the real constraint on the west half of the board, not the pin field.
