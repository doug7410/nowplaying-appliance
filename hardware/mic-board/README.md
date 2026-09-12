# Now-Playing mic board

Adapter that mounts the proven round **INMP441 I2S MEMS breakout** (Ø14 mm, two
1×3 rows at 7.62 mm) and brings it out to a **6-pin JST-XH** matching the carrier
board's `J_MIC` pinout, 1:1 cable:

| J1 pin | Net | | J1 pin | Net |
|---|---|---|---|---|
| 1 | MIC_SCK | | 4 | GND |
| 2 | MIC_WS | | 5 | P3V3 |
| 3 | MIC_SD | | 6 | GND |

- **L/R is tied to GND on this board** (left channel) — matches WLED-MM
  `digitalmic.type=1` and the Board 1 smoketest wiring.
- Mount the module **label/port side up** (chip side toward this board) on the
  supplied pin headers; the adapter's silk row labels then match the module's.
  The Ø3 mm center hole is acoustic relief so either mounting orientation works.
- Two M3 holes, 14 mm apart, at the top corners.

Built headlessly (kicad-cli 10); `make check` runs ERC + DRC (0 violations,
schematic parity clean), `make zip` builds `mic-board-jlc.zip` for JLCPCB
(2 layers, 20 × 38 mm, 1.6 mm, HASL — bare board, hand-solder the two headers
and the XH).
