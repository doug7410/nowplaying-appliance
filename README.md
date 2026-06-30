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

## On the Pi

`pi/` is the platform layer the sim's `sim/` is to SDL: LVGL on the Linux **DRM/KMS**
(default) or **fbdev** display driver + **evdev** touch — no compositor, no browser. It
reuses `src/ui.cpp`, `src/now_playing.h`, and `sim/backend.cpp` unchanged; only the entry
point (`pi/main.cpp`) and `pi/lv_conf.h` differ from the sim.

Deploy mirrors the kiosk (`provision.sh` + a systemd unit), run **on the Pi**:

```sh
cp pi/np.env.example pi/np.env     # set NP_TOKEN (the SHARED appliance token) + NP_BASE_URL
./provision.sh                     # apt deps -> build -> install + start np-display.service
#   DISPLAY_BACKEND=fbdev ./provision.sh   # if DRM won't win master on the VT
```

Notes:
- Token/host come from `pi/np.env` via the unit's `EnvironmentFile` (out of git + the binary);
  `backend.cpp` reads `NP_TOKEN`/`NP_BASE_URL` env, falling back to `src/secrets.h`.
- DRM master is exclusive — the unit takes tty1 (`Conflicts=getty@tty1`, `PAMName=login`)
  and `provision.sh` disables the old `np-kiosk`. fbdev sidesteps the master fight.
- Touch panel = `/dev/input/event0` (QDtech MPI5001); override with `NP_TOUCH_DEV` if it moves.
- Build only the app target: `cmake --build pi/build --target np-display` (skips lvgl demos).
