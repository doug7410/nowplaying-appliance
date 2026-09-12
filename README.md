# Now-Playing appliance

A desk device that listens to the room, figures out what song is playing, and shows it:
a HUB75 LED matrix renders a synced spectrum / lyrics visualization, and a 5" touch
screen shows the cover, title, artist, progress and a mosaic of what played recently.
The whole thing lives in a routed guitar body with three gold knobs and a bat-handle
power toggle.

```
                       room audio
                           │
             ┌─────────────▼──────────────┐
             │  BOARD 1 — the room ear     │  ESP32-S3 running a WLED-MM fork:
             │  I2S mic · HUB75 64×32      │  spectrum / Keybeat / lyrics looks,
             │  panel · 3 knobs            │  uploads mic windows to the cloud
             └──────┬───────────────▲──────┘
      POST /ingest  │               │  GET /lyrics
                    │               │              UART (knobs, re-check, heartbeat)
             ┌──────▼───────────────┴──────┐        │
             │  CLOUD — recognition brain   │        │
             │  songrec → now-playing state │        │
             │  → metadata, lyrics, covers  │        │
             └──────┬───────────────▲──────┘        │
     GET /nowplaying│               │ GET /album/{id}│
             ┌──────▼───────────────┴──────┐        │
             │  BOARD 2 — the touch display │◄───────┘
             │  Raspberry Pi + 5" HDMI/USB  │  native LVGL app (display/)
             └─────────────────────────────┘
```

Both boards authenticate to the cloud with **one shared device token**. The cloud
derives all state server-side; the boards render dumb.

## What is in this repo

| Path | What |
|---|---|
| [`BOM.md`](BOM.md) | The full parts list, with prices and sourcing notes |
| [`docs/assembly.md`](docs/assembly.md) | Order the PCBs, solder them, wire the harness, drill the front panel, first power-on |
| [`docs/board1-firmware.md`](docs/board1-firmware.md) | Build, flash and provision the ESP32-S3 "ear" |
| [`docs/board2-display.md`](docs/board2-display.md) | Set up the Raspberry Pi and run the display app |
| [`docs/cloud.md`](docs/cloud.md) | Running the recognition service and minting the shared token |
| `hardware/carrier-board/` | KiCad source + JLCPCB zip for the interconnect PCB (ESP32-S3 socket, power, HUB75, knobs, mic, Pi) |
| `hardware/knob-board/` | KiCad source + zip for the 3-encoder front-panel PCB. Also the front-panel drilling template |
| `hardware/mic-board/` | KiCad source + zip for the INMP441 breakout adapter |
| `hardware/panelboard/` + [`docs/panelboard.md`](docs/panelboard.md) | **The minimal build:** a standalone HUB75 driver board for a stock ESP32-WROOM-32 dev board, runs upstream WLED-MM unmodified. Just the LED matrix, none of the rest |
| `display/` | The Board 2 display app (LVGL, C++). Runs on the Pi via DRM/KMS, and on a desktop as an SDL simulator |

## Build order

Only want the LED spectrum on a panel? Build the [panelboard](docs/panelboard.md)
instead: one small PCB, a $5 ESP32 dev board, upstream WLED-MM, done. The rest of this
list is the full appliance.

1. Read [`BOM.md`](BOM.md) and order everything. The three PCBs are bare boards from
   JLCPCB, roughly $20 for five of each, hand-soldered.
2. [`docs/assembly.md`](docs/assembly.md): solder the boards, make the four cables, wire
   power, mount the panel and screen.
3. [`docs/cloud.md`](docs/cloud.md): stand up the cloud service and create **one** device
   token. Both boards use it.
4. [`docs/board1-firmware.md`](docs/board1-firmware.md): flash the ESP32-S3 and run the
   provisioning script. The panel should light up with the "Mountain" spectrum.
5. [`docs/board2-display.md`](docs/board2-display.md): image the Pi, run `provision.sh`
   in `display/`. Play a song in the room.

## Status and what is not here yet

This is a working one-off appliance and the design for a small hand-built batch. Every
board in `hardware/` has been fabbed and passes ERC/DRC with zero violations; the pin
maps and current budgets are measured on the bench, not estimated.

Two source trees the build depends on are **not published yet**:

- **The Board 1 firmware** is a fork of [WLED-MM](https://github.com/MoonModules/WLED-MM)
  carrying a `room_mic_uploader` usermod, the Keybeat and lyrics looks, and the HUB75 pin
  map. [`docs/board1-firmware.md`](docs/board1-firmware.md) documents the build exactly
  so it can be reproduced once the fork is pushed.
- **The cloud service** (Laravel + `songrec`). Its HTTP contract is documented in
  [`docs/cloud.md`](docs/cloud.md); the display app in `display/` codes against it.

## License

See [`LICENSE`](LICENSE).
