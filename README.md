# nowplaying-display

Native **LVGL** display app for the Now-Playing appliance's Board 2 (the touch screen).
Replaces the Chromium kiosk (`~/Code/nowplaying-pi`) so it fits a 512 MB Pi
(Zero 2 W / 3A+) — LVGL's working set is ~30–60 MB vs Chromium's ~450 MB.

Renders the same four-screen 800×480 design (Now Playing / Listening / Recently / Album)
the kiosk built, fed by the frozen cloud contract (`GET /nowplaying`, `GET /album/{id}`).

## Layout

- `src/ui.cpp` — the LVGL UI (the screens). The only file with the new design in it.
- `src/ui.h` / `ui_platform.h` — UI public surface + the host shims it needs.
- `src/now_playing.h` — the cloud contract structs (`NowPlaying`, `Album`, …).
- `src/uart_link.h` — codec for the deferred ESP32↔Pi knob/recheck bridge.
- `sim/` — SDL desktop simulator: runs `src/ui.cpp` in an 800×480 window against the
  **live** cloud (libcurl + stb_image in `backend.cpp`). This is how you see it without
  hardware.

## Run the sim

```sh
cp src/secrets.h.example src/secrets.h   # fill in BASE_URL + the SHARED appliance token
cd sim && ./run.sh                        # first run fetches LVGL + stb_image + json
```

Needs SDL2 + libcurl dev packages. Mouse = touch; drag to page the mosaic.

## On the Pi (later)

LVGL runs on DRM/KMS or fbdev — no compositor or browser. Deploy mirrors the kiosk:
a `provision.sh` + systemd unit. Not built yet.
