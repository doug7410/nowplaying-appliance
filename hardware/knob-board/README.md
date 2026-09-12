# Knob board (KiCad)

The Board-1 front-panel knob PCB: three EC11 rotary encoders (MODE / BRIGHT / GAIN)
plus a switch resistor ladder, out to a single 8-pin JST-XH connector.

This is a **re-creation in KiCad** of the design that was routed in Flux
(`https://www.flux.ai/doug7410/nowplaying-knob-board~ss`). Flux cannot export
`.kicad_pcb`/`.kicad_sch` — only Gerbers, IPC-2581, ODB++, GenCAD and EDIF, none of
which KiCad imports — so the schematic and layout were rebuilt from the Flux Gerber
and IPC-D-356 netlist exports. The KiCad files here are now the source of truth.

## The circuit

Ten nets, six parts. Every footprint is stock KiCad — no custom library.

| Ref | Part | Footprint |
|---|---|---|
| ENC1/2/3 | Alps `EC11E15244G1` | `Rotary_Encoder:RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm` |
| J1 | JST `B8B-XH-A(LF)(SN)` | `Connector_JST:JST_XH_B8B-XH-A_1x08_P2.50mm_Vertical` |
| R1 | 3.3 kΩ axial | `Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal` |
| R2 | 10 kΩ axial | same |

**J1 pinout — the harness contract:**

```
1 MODE_A · 2 MODE_B · 3 BRIGHT_A · 4 BRIGHT_B · 5 GAIN_A · 6 GAIN_B · 7 SW_LADDER · 8 GND
```

**The switch ladder.** Three push-switches share one ADC pin. Each switch's common
ties to `SW_LADDER`; the other side goes to GND through a distinct resistance —
ENC1 direct (0 Ω), ENC2 via R1 3.3 kΩ, ENC3 via R2 10 kΩ. The 10 kΩ pull-up to 3V3
and the 100 nF debounce cap live **on the carrier board**, not here; that is why
there is no 3V3 net and the cable is 8 conductors. Levels at GPIO 2 (`ADC1_CH1`):
idle 3.30 V, GAIN 1.65 V, BRIGHT 0.82 V, MODE 0.00 V.

On the KiCad symbol, `A`/`B`/`C` are the rotary section (C is the rotary common →
GND) and `S1`/`S2` are the push-switch. Flux's part model called the same pins
`TERMINAL_A/B/C` and `COM`/`NO`; easy to conflate.

## Geometry

Board **100.0 × 30.0 mm**, rect `(10,10)`–`(110,40)` in KiCad coordinates, centre
`(60,25)`. That frame is deliberately the same one the Flux Gerbers use (Gerber Y is
the negation), so the two can be compared hole-for-hole.

Knob shafts sit at **X = centre −38 / 0 / +38** (38 mm pitch) and **Y = 5 mm above
centre**. The PCB is the front-panel drilling template, so these are the numbers that
have to match the enclosure.

Encoders are on the top layer at 180°; J1, R1 and R2 are on the bottom. The bottom is
a GND pour; the top carries the six quadrature signals.

**Routing.** Pads occupy Y 14.4–25.6 (encoders) and Y 34.59 (J1), leaving a clear
corridor at Y 28–33 across the whole board. Each quadrature signal takes a horizontal
lane there and drops onto its J1 pin; lanes are ordered so no drop ever crosses a lane
still carrying copper at that X. `SW_LADDER` busses all three switch commons and would
have to cross every one of those lanes, so it runs on the **bottom** layer instead —
it just carves a channel through the GND pour, which reconnects around both ends.

## Differences from the Flux board (v2, `a6fcbc93`)

Verified by diffing the drill files:

- **J1, R1, R2 holes are identical** — all 12, exactly.
- **All 15 encoder pins are identical after a deliberate 0.85 mm X shift.** Flux's
  re-route nudged the knob group 0.85 mm right of centre, which broke the left/right
  symmetry (12.85 mm from one edge, 11.15 mm from the other). Here the knobs are
  symmetric: 12.0 mm from each edge.
- **Mounting lugs**: same X, 0.1 mm different in Y, drilled round 2.6 mm vs Flux's
  round 3.0 mm.

This board passes DRC with **0 violations, 0 unconnected pads and 0 schematic-parity
issues**, and JLC's DFM check clean — see below.

## JLC DFM

Ran 2026-08-05 on `knob-board-jlc.zip`: **zero Danger findings in every category** —
no trace/pad spacing, annular ring, edge clearance, soldermask or drill problems.
(The Flux board, by contrast, flagged 21 silkscreen line-width errors, 12
silkscreen-to-pad/hole errors and 11 soldermask-openings-exposing-trace at 0.09 mm.)

Two warnings came back on the first run and are both fixed:

- **Silkscreen line width 0.12 mm (×54).** Stock KiCad footprints ship 0.12 mm silk.
  Everything on F/B.Silkscreen is widened to **0.16 mm** at build time (`SILK_W`), text
  included. Note the exact threshold: **JLC's minimum is 0.153 mm (6 mil), not 0.15** —
  a first attempt at 0.15 mm came back with all 54 warnings intact.
- **Short slot detection 1.3 mm (×6).** The encoder mounting lugs were an oval
  2.8 × 1.5 mm slot; JLC wants a slot at least twice as long as it is wide, and this
  one routed only 1.3 mm. Switched to the stock
  `..._CircularMountingHoles` footprint variant — identical pad positions, but a plain
  round 2.6 mm drill. **The board now has no routed slots at all**, which some fabs
  surcharge for.

If you regenerate the Gerbers, note `make zip` runs `make check` first, so a parity or
DRC regression stops the build rather than silently shipping a stale `gerbers/`.

## Rebuilding

```
make check      # ERC + DRC (with schematic parity); fails on any violation
make gerbers    # gerbers/ + Excellon drill
make zip        # knob-board-jlc.zip — upload this straight to jlcpcb.com
```

`make zip` depends on `check`, so a board that fails DRC can never reach the fab.

**Ordering at JLCPCB:** upload `knob-board-jlc.zip`, then **2 layers, 100 × 30 mm,
1.6 mm, HASL** — every other default is fine. It's a plain 2-layer through-hole board
with no controlled impedance, no blind vias and nothing below 0.35 mm.

Requires `kicad-cli` and the `kicad-library` package (stock symbols and footprints).

Generated output (`gerbers/`, `knob-board-jlc.zip`, `*.rpt`, `*.net`) is gitignored —
the `.kicad_sch` and `.kicad_pcb` are the artifacts that matter.

## Not done

- No silkscreen legend (MODE / BRIGHT / GAIN) next to the knobs — assumed to live on
  the front panel. Say so if the bare board should carry it.
- R1/R2 have no MPN. Irrelevant unless ordering assembly; these are hand-soldered.
