# Panelboard: just the LED matrix

The smallest build in this repo. One 70 × 72 mm board, one stock ESP32 dev board plugged
into it, one HUB75 panel: 5 V in, spectrum out. No mic on the board, no knobs, no
display, no cloud. Upstream WLED-MM drives it unmodified because the board is wired to
WLED-MM's default classic-ESP32 HUB75 pin map (the ESP32-Trinity map).

Design notes, the pin table, and the layout rationale are in
[`hardware/panelboard/README.md`](../hardware/panelboard/README.md). This page is the
build recipe.

## Parts

| Part | Qty | Notes |
|---|---|---|
| Panelboard PCB, `hardware/panelboard/panelboard-jlc.zip` | 1 | JLCPCB: 2 layer, 70 × 72 mm, 1.6 mm, HASL, defaults otherwise. ~$4 for five, bare board |
| **ESP32-WROOM-32 38-pin dev board**, narrow type (DevKitC-32 / HiLetgo / DORHEA, USB-C or micro) | 1 | Classic ESP32, **not** S3. Row spacing must be **0.9" / 22.86 mm**; measure before ordering, a 1.0" clone will not seat |
| 64×32 P4 HUB75 panel, 1/16 scan, with its ribbon and power pigtail | 1 | Same panel as the full appliance |
| 5 V supply, ≥ 3 A, and a way to get it to a screw terminal | 1 | Panel worst case is 2 A (solid white, full brightness); real looks draw ~0.3 A |
| J_ESP_L, J_ESP_R: 1×19 female header, 2.54 mm | 2 | |
| J_HUB: 2×8 shrouded IDC box header, vertical | 1 | |
| J_PWR, J_PANEL: screw terminal, 2 position, 5.00 mm (Phoenix MSTBVA-compatible) | 2 | |
| JP1: 1×2 male header + shunt | 1 | |
| Q1: AOD403 P-channel MOSFET, TO-252 | 1 | The only surface-mount part |
| RG: 100 kΩ axial, RPD: 10 kΩ axial | 1 each | |
| CB1: 470 µF 16 V radial D8, CB2: 100 nF disc | 1 each | |

## Solder

1. **RPD first.** It sits under the module and is unreachable once the sockets are in.
   It holds IO12 (the G2 line, also the MTDI strapping pin) low at reset; without it
   the chip can come up expecting 1.8 V flash and fail to boot.
2. Q1: tin the tab pad, place, reflow the tab, then the legs. Then RG, CB2, CB1
   (polarity band on the silk).
3. Screw terminals, JP1, the IDC header (key notch as silk-screened).
4. Plug the dev board into both 1×19 sockets, then solder the sockets with the module
   as the alignment jig.

## Wire

```
5 V supply ─► J_PWR ─► Q1 (reverse-polarity) ─► P5V ─┬─► J_PANEL ─► panel power pigtail
                                                     └─► JP1 ─► module 5 V pin
J_HUB ─► panel ribbon ─► panel HUB75 IN
```

The 5 V copper is 2.5 mm wide throughout, sized for the panel's 2–4 A.

**Pull the JP1 shunt before plugging USB into the dev board** while J_PWR is live. The
module's 5 V pin has no reverse-current protection, so a live rail plus a USB cable
back-feeds your computer. Flash over USB with JP1 out, then put the shunt back.

## Firmware

Upstream WLED-MM, branch `mdev`, stock target. Nothing in this repo's Board 1 build
applies: that one needs 16 MB flash and PSRAM, and a plain WROOM-32 has 4 MB and none.

```sh
git clone -b mdev https://github.com/MoonModules/WLED-MM.git && cd WLED-MM
pio run -e esp32_4MB_V4_S_HUB75 -t upload     # JP1 out, USB in
```

That target's HUB75 pinout is the one the board is wired to; do not add a pinout
override. A 64×32 DMA framebuffer fits in internal RAM; 64×64 at high bit depth will
not (the board does wire the E line, so a 64×64 panel is only a config change).

Then join the board to WiFi (WLED-AP captive portal) and, in the web UI:

- **Config → LED Preferences:** LED type **HUB75**, length **2048**, the "pin" field
  is the panel chain length (**1**), colour order **RGB**. The Waveshare P4 panel swaps
  red and green under WLED's default GRB.
- **Config → 2D Configuration:** one panel, **64 × 32**.
- **Config → Sound Settings:** whatever source you have; a UDP sound-sync sender on the
  LAN, or an I2S mic wired to the module's free pins: GPIO 32, 33, 21, 22 are
  unconnected outputs (SCK, WS), and the input-only 34–39 take the data line (SD).
- Pick an audio-reactive 2D effect (GEQ, Waverly, 2D Swirl...) and save it as the boot
  preset.

Those settings live in the board's flash config, not the binary. Export them from
Config → Security & Updates → Backup once you like them.
