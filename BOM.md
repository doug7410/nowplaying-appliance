# Bill of materials

One appliance = Board 1 (ESP32-S3 "ear") + Board 2 (Raspberry Pi display) + the
interconnect PCBs + one enclosure. Prices are USD, small-quantity retail, rounded. Where
a part is a specific SKU that matters, it says so and why.

The custom PCBs are ordered as **bare boards** and hand-soldered. Every part on them is
through-hole except one DPAK MOSFET, which hand-solders fine.

## Board 1 — the ear

| # | Part | Qty | ~$ | Notes |
|---|---|---|---|---|
| 1 | **ESP32-S3-DevKitC-1**, genuine Espressif: **N16R8** (16 MB flash, 8 MB octal PSRAM) or **N8R8** (8 MB flash) | 1 | 15 | Plugs into the carrier's two 1×22 sockets. **Must be the genuine Espressif board with 22.86 mm (0.9") row spacing.** Every Amazon "N16R8 DevKitC-1" listing is a clone with 1.0" rows and will not seat. Buy from Mouser: N8R8 is stocked (`356-EP32S3DVKTC1N8R8`); N16R8 (`356-ESP32S3DKC1N16R8`) is factory-order. N8R8 needs the 8 MB partition table, see the firmware page; the firmware and assets fit either. |
| 2 | **64×32 P4 HUB75 LED matrix**, 1/16 scan, e.g. Waveshare RGB-Matrix-P4-64x32 | 1 | 30 | Buy a branded, restockable SKU new. Comes with the 16-pin ribbon and the power pigtail. Driven at 3.3 V logic with no level shifter, bench-verified. |
| 3 | **INMP441 I2S MEMS mic breakout**, the round Ø14 mm module with two 1×3 rows 7.62 mm apart | 1 | 3 | The mic board is cut for this exact module. INMP441 is EOL at TDK but modules are plentiful. Do not substitute SPH0645: WLED-MM's SPH0645 timing fix is classic-ESP32 only and is a no-op on the S3. |
| 4 | Carrier board PCB, `hardware/carrier-board/carrier-board-jlc.zip` | 1 | 11 / 5 pcs | JLCPCB: 2 layer, 140 × 100 mm, 1.6 mm, 1 oz, HASL. Everything else default. |
| 5 | Knob board PCB, `hardware/knob-board/knob-board-jlc.zip` | 1 | 2 / 5 pcs | JLCPCB: 2 layer, 100 × 30 mm, 1.6 mm, HASL. |
| 6 | Mic board PCB, `hardware/mic-board/mic-board-jlc.zip` | 1 | 4 / 5 pcs | JLCPCB: 2 layer, 20 × 38 mm, 1.6 mm, HASL. |
| 7 | Carrier board parts (table below) | 1 set | 8 | |
| 8 | Knob board parts (table below) | 1 set | 6 | |
| 9 | Mic board parts (table below) | 1 set | 1 | |
| 10 | Knob caps, 6 mm D-shaft, set-screw, gold knurled dome | 3 | 6 | Guitar-knob aesthetic. |
| 11 | **Master power toggle**: SPST bat-handle, rated ≥10 A DC, gold, with a Gibson-style "poker chip" ring | 1 | 6 | Switches the 5 V rail, never mains. RHYTHM up = ON. |
| 12 | **Panel-mount DC jack**, DC-099 style, 5.5 × 2.1 mm, threaded | 1 | 2 | Bolts through the enclosure wall so plug force lands on the wood, not a PCB. |
| 13 | **5 V power brick**, 5.5 × 2.1 mm plug, **≥ 6 A** (10 A gives headroom) | 1 | 25 | Measured budget: panel 2.0 A worst case (solid white, full brightness; 0.3 A in real use), Pi up to 3 A, ESP32 0.5 A. The brick stays outside the enclosure. |

### Carrier board parts

Matches `hardware/carrier-board/assembly/bom-jlc.csv` (LCSC part numbers are in that
file). All through-hole except Q1.

| Ref | Part | Qty | Footprint |
|---|---|---|---|
| Q1 | AOD403 P-channel MOSFET (reverse-polarity protection) | 1 | TO-252 / DPAK |
| R1–R7 | 10 kΩ ¼ W axial | 7 | axial, 7.62 mm pitch |
| RG | 100 kΩ ¼ W axial | 1 | axial |
| C1–C6 | 10 nF 50 V X7R leaded ceramic | 6 | disc, 5 mm pitch |
| C3V3, C7, CB2 | 100 nF X7R leaded ceramic | 3 | disc, 5 mm pitch |
| CB1 | 470 µF 10 V electrolytic | 1 | radial D8, 3.5 mm pitch |
| J_PWR, J_SW, J_PANEL | Screw terminal, 2 position, 5.00 mm pitch | 3 | Phoenix MSTBVA-compatible (KF2EDGVC-5.0-2P clones are fine) |
| J_KNOBS | JST B8B-XH-A | 1 | XH 2.5 mm, 8 pin |
| J_MIC | JST B6B-XH-A | 1 | XH, 6 pin |
| J_PI | JST B5B-XH-A | 1 | XH, 5 pin |
| JP1, JP2, JP3 | 1×2 pin header 2.54 mm **+ 3 shunts** | 3 | |
| J_ESP_L, J_ESP_R | 1×22 female pin socket 2.54 mm | 2 | The ESP32-S3 devkit plugs in here |
| J_HUB | 2×8 IDC box header 2.54 mm, shrouded, with key notch | 1 | The panel ribbon plugs in here |

### Knob board parts

| Ref | Part | Qty |
|---|---|---|
| ENC1–3 | Alps **EC11E15244G1** rotary encoder with push switch, 20 mm shaft, threaded bushing | 3 |
| J1 | JST B8B-XH-A | 1 |
| R1 | 3.3 kΩ ¼ W axial | 1 |
| R2 | 10 kΩ ¼ W axial | 1 |

### Mic board parts

| Ref | Part | Qty |
|---|---|---|
| J1 | JST B6B-XH-A | 1 |
| — | 1×3 pin header, 2.54 mm (mounts the INMP441 module) | 2 |

### Cables

| Cable | From → to | Notes |
|---|---|---|
| Knobs | carrier J_KNOBS → knob board J1 | 8-conductor JST-XH, **straight through 1:1** |
| Mic | carrier J_MIC → mic board J1 | 6-conductor JST-XH, straight through |
| Pi | carrier J_PI → Pi 40-pin header pins 2, 4, 6, 8, 10 | 5-conductor: XH housing on one end, 2.54 mm female Dupont on the other. Straight through; the UART crossover is on the carrier. |
| Panel data | carrier J_HUB → panel HUB75 IN | The 16-pin ribbon that ships with the panel |
| Panel power | carrier J_PANEL → panel power connector | The pigtail that ships with the panel, into the screw terminal. Do not run panel current through anything thinner. |
| DC in | jack lugs → carrier J_PWR; toggle → carrier J_SW | 18 AWG or heavier, carries the full ~6 A |

JST-XH housings + crimp contacts (or pre-crimped XH leads) for 8, 6 and 5 positions.

## Board 2 — the display

| # | Part | Qty | ~$ | Notes |
|---|---|---|---|---|
| 1 | **Raspberry Pi** with a 40-pin header and HDMI out | 1 | 35–55 | The display app is native LVGL (~30–60 MB), so RAM is not a constraint. **Verified on a Pi 3B**; a Pi 4 (2 GB) is the pick for a new build (in production, supports USB boot). A Zero 2 W is the app's design target but not yet hardware-verified, and needs a mini-HDMI adapter and a micro-USB OTG cable for the touch panel. |
| 2 | microSD, 16 GB+, endurance grade (or a USB SSD on a Pi 4) | 1 | 8 | The master toggle is a hard power cut. Use read-only root (overlayfs) or USB boot so cuts are safe. |
| 3 | **5" 800×480 IPS HDMI display with USB capacitive touch**, USB-powered (Hosyond; the touch controller enumerates as QDtech MPI5001) | 1 | 40 | HDMI video + USB touch = zero GPIO use. **Avoid** the DSI variant and resistive eBay units. Verify the listing says capacitive. |
| 4 | Short HDMI cable (Pi 3B/4: full/micro HDMI to HDMI as appropriate) | 1 | 4 | |
| 5 | Short USB-A to micro-USB cable | 1 | 2 | Screen power + touch, from a Pi USB port |

The Pi has **no separate power supply**: it is fed 5 V on header pins 2 and 4 from the
carrier board through the master toggle. Powering over the header bypasses the Pi's
input fuse and polyfuse, which is fine here because the carrier's MOSFET and the brick's
own protection are upstream.

## Enclosure

| Part | Notes |
|---|---|
| Double-cut guitar body blank (or a used body) | Displays inset into the top, control cavity routed from the rear, jack-plate power inlet on the rim |
| Front acrylic: **TAP Plastics Chemcast "Black LED" 1/8"** | Black when off, transmits when lit. **Not** "Black Opaque" (0 % transmission). Gray smoke acrylic is the cheap substitute. |
| Diffuser: **TAP frosted P95 "Lighting White" 69 % transmission, 1/8"** | **Not** Matte White 9 % (too dim), **not** clear (hot spots). |
| M2.5 nylon standoff kit | Stack to a ~5 mm air gap between panel and diffuser. Tune softness with the gap. |
| Passive heatsink for the Pi | **No fan**: the mic would hear it. Vent the cavity. |
| Fasteners, hookup wire, heat-shrink | |

## Panelboard: the matrix-only build

Not part of the appliance; an alternative to it. One PCB, one stock ESP32-WROOM-32 dev
board, one panel, one 5 V supply. Full parts table and recipe in
[`docs/panelboard.md`](docs/panelboard.md).

| Bucket | ~$ |
|---|---|
| Panelboard PCB (5 pcs) + parts | 12 |
| ESP32-WROOM-32 38-pin dev board (narrow, 0.9" rows) | 5 |
| 64×32 P4 HUB75 panel | 30 |
| 5 V ≥3 A supply | 10 |
| **Total** | **~$57** |

## Roll-up

| Bucket | ~$ |
|---|---|
| Board 1 electronics (incl. PCBs and parts) | 115 |
| Board 2 electronics | 95 |
| Cables, switch, jack, brick | 35 |
| Enclosure materials | 40–130 |
| **Total materials** | **~$285–375** |

Cloud hosting is an operating cost shared across all appliances, not a per-unit part.
