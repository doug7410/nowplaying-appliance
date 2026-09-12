# Board 1 firmware: the ESP32-S3 ear

Board 1 runs a fork of [WLED-MM](https://github.com/MoonModules/WLED-MM) (MoonModules'
audio-reactive WLED, branch `mdev`) with these additions:

| Piece | What it does |
|---|---|
| `usermods/room_mic_uploader/` | Taps the audio-reactive PCM stream, POSTs mic windows to the cloud's `/ingest`, reads the three EC11 knobs (quadrature in a pin-change ISR), talks to the Pi over UART (knob events, re-check, heartbeat), fetches synced lyrics for the on-panel lyrics look |
| `wled00/FX.cpp` fx 230 "Equalizer LF" | The "Mountain" spectrum: 16 bands, each shown as energy above its own rolling mean |
| `wled00/keybeat_loader.cpp` | Loads beat-synced GIF looks from LittleFS |
| `wled00/bus_manager.cpp` `SECTION9_2_PINOUT` | The HUB75 pin map the carrier board is wired to |
| `pio-scripts/patch_audioreactive.py` | Re-applies a vendored, DSP-patched copy of the audio-reactive usermod after every clean |
| `board1-config/provision-board1.sh` | Post-flash provisioning of everything that lives in WLED's runtime config |

> **Not yet public.** The fork and the vendored assets it pulls from the appliance repo
> (`audio_reactive.h`, the Mountain palette/preset, the Keybeat GIF presets) have not been
> pushed to GitHub yet. Everything below is the exact recipe that builds the shipping
> board, written so it works unchanged once they are. The upstream WLED-MM build alone
> will drive the panel but will not ingest, show lyrics, or talk to the Pi.

## Prerequisites

- PlatformIO (`pip install platformio` or the VS Code extension).
- The ESP32-S3-DevKitC-1 N16R8 on USB (`/dev/ttyACM0` on Linux). **Pull JP1 on the
  carrier first** if the devkit is seated and the carrier is powered.
- A running cloud and the shared device token ([`cloud.md`](cloud.md)).

## 1. Configure the build

Two gitignored files carry the secrets. Create both in the fork's checkout.

`platformio_override.ini`:

```ini
[env:board1]
extends = env:esp32S3_8MB_PSRAM_M_opi

board_build.flash_size = 16MB
board_upload.flash_size = 16MB
board_build.partitions = ${esp32.extreme_partitions}   ; 16MB: 3.2MB firmware, 9MB FS

upload_port = /dev/ttyACM0
monitor_port = /dev/ttyACM0
upload_speed = 921600
monitor_speed = 115200
monitor_filters = esp32_exception_decoder

build_flags = ${env:esp32S3_8MB_PSRAM_M_opi.build_flags}
  -D WLED_RELEASE_NAME=nowplaying_board1
  -D SECTION9_2_PINOUT              ; HUB75 pins in bus_manager.cpp (matches the carrier)
  -D SR_ENABLE_DEFAULT              ; audioreactive on from first boot
  -D USERMOD_ROOM_MIC_UPLOADER      ; cloud ingest + knobs + UART + lyrics
  -D CLIENT_SSID='"your-wifi-ssid"'
  -D CLIENT_PASS='"your-wifi-password"'
  ; I2S MEMS mic on the carrier: SCK=38 WS=39 SD=40, no MCLK
  -D SR_DMTYPE=1 -D I2S_SDPIN=40 -D I2S_CKPIN=38 -D I2S_WSPIN=39 -D MCLK_PIN=-1
  ; keep WLED's default LED/relay buses off the HUB75 / mic / knob pins
  -D LEDPIN=2 -D RLYPIN=-1

lib_deps = ${env:esp32S3_8MB_PSRAM_M_opi.lib_deps}
```

`usermods/room_mic_uploader/room_mic_uploader_secrets.h` (copy from the `.example`):

```c
#define NP_HOST         "cloud-host-or-ip"   // no scheme; the POST goes over a raw socket
#define NP_PORT         8000
#define NP_INGEST_PATH  "/api/ingest"
#define NP_DEVICE_TOKEN "<the shared appliance token>"   // SAME token as Board 2
```

The vendored `audio_reactive.h` must be reachable at
`../nowplaying/firmware/board1/wled-mm/audio_reactive.h` relative to the fork, or point
`NP_VENDORED_AUDIOREACTIVE` at it. The build fails loudly if it is missing rather than
silently shipping the stock look.

## 2. Build and flash

```sh
pio run -e board1 -t erase  --upload-port /dev/ttyACM0   # clears stale runtime config
pio run -e board1 -t upload --upload-port /dev/ttyACM0
pio device monitor                                        # watch it join WiFi, note the IP
```

## 3. Provision the runtime config

Some of what makes the board work lives in WLED's runtime config on flash, not in the
binary: the HUB75 bus and 2D layout, the **RGB colour order** (this panel swaps red and
green under WLED's GRB default), the local-I2S mic mode with `I2S_FastPath=0`, the boot
preset, the Mountain palette, and the Keybeat GIF assets. A flash erase wipes all of it.
One script restores it:

```sh
./board1-config/provision-board1.sh <board-ip>
```

It POSTs the config, uploads the palette, presets and GIFs, reboots, and verifies
`leds.count == 2048`, audio source `I2S digital`, and boot preset 3 (fx 230, palette
255). It prints `PASS` when the panel is rendering the Mountain look in correct colours
and the mic is ingesting. Idempotent; re-run any time. The config then survives reboots
and OTA updates.

## Knobs

Three bare EC11 encoders: MODE on GPIO 47/48, BRIGHT on 1/12, GAIN on 41/42. Detents
per encoder batch vary (1, 2 or 4 quadrature transitions per click); the divisor is a
calibration constant at the top of `room_mic_uploader.h`. All three push switches share
GPIO 2 through the resistor ladder on the knob board (idle 3.3 V, GAIN 1.65 V, BRIGHT
0.82 V, MODE 0 V). Today only the MODE press does something (jump home); enabling the
other two is a firmware-only change.

The MODE knob cycles looks in the order given by `/npcycle.json` on the board's
filesystem (seeded from `board1-config/npcycle-default.json`). Reorder looks without a
reflash.

## Gotchas that cost real time

- **Panel dark, or red shows green**: runtime config was wiped. Run the provisioning
  script.
- **Mic reads silence (RMS 0)** with a good mic: either `I2S_FastPath` is 1 (the script
  sets 0), or the mic was reseated on a running board. **Reboot after reseating**; I2S
  never recovers from a reconnect on a live board.
- **Panel ghosting**: not seen on this panel at 3.3 V. If a different panel ghosts, put
  an off-the-shelf HUB75 buffer board inline on the ribbon; there is no room for one on
  the carrier.
- **The board and the Pi cannot see each other over WiFi** on many consumer APs (client
  isolation). That is why the knob / re-check link is a UART wire and why the Pi only
  ever talks to the cloud.
