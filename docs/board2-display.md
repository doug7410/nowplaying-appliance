# Board 2: the Raspberry Pi display

Board 2 is a Raspberry Pi driving the 5" 800×480 HDMI touch panel with the native
LVGL app in [`display/`](../display/README.md). No desktop, no compositor, no browser:
the app draws straight to the screen through DRM/KMS on tty1 and reads touch from evdev.
It polls the cloud's `GET /nowplaying` every few seconds and `GET /album/{id}` when a
cover is tapped, and renders four screens: Now Playing, Listening (idle), Recently
(cover mosaic), Album.

Verified on a Pi 3B. Any Pi with a 40-pin header and HDMI out works; the app's working
set is 30–60 MB.

## 1. Image the Pi

Raspberry Pi OS **Lite** (Debian 12 or 13), 64-bit. In the imager set a hostname, a
user, WiFi, and enable SSH. Boot it headless and `ssh` in.

## 2. Free the UART for the Board 1 link

The carrier's J_PI cable puts Board 1's serial on header pins 8/10. Take Bluetooth off
the good UART and stop the kernel using it as a console:

```sh
sudo sed -i 's/ console=serial0,115200//' /boot/firmware/cmdline.txt
printf 'enable_uart=1\ndtoverlay=disable-bt\n' | sudo tee -a /boot/firmware/config.txt
sudo systemctl disable serial-getty@ttyS0 serial-getty@ttyAMA0
sudo reboot
```

After the reboot `/dev/serial0` is the PL011 (`ttyAMA0`), 115200 8N1.

## 3. Build and install the display app

On the Pi:

```sh
git clone <this repo> && cd nowplaying-appliance/display
cp pi/np.env.example pi/np.env
$EDITOR pi/np.env          # NP_BASE_URL = http://<cloud-host>:8000/api
                           # NP_TOKEN    = the SHARED appliance token (same as Board 1)
./provision.sh             # apt deps → cmake build → np-display.service on tty1
```

`provision.sh` is idempotent: re-run it after pulling changes. It installs a systemd
unit that owns tty1 (`Conflicts=getty@tty1`, `PAMName=login` so the process gets a
logind seat and can take DRM master) and restarts on failure. Logs:

```sh
journalctl -u np-display -f
```

If DRM will not initialise on your Pi/OS combination, `DISPLAY_BACKEND=fbdev
./provision.sh` writes `/dev/fb0` directly instead. It tears slightly but never fights
for DRM master.

The touch panel enumerates as `/dev/input/event0` (QDtech MPI5001). If it lands
elsewhere, set `NP_TOUCH_DEV` in `pi/np.env`.

## 4. The one trap: the token

`/nowplaying` and the recent mosaic are scoped **per device**. If the Pi uses a token
other than the one Board 1 ingests under, it reads an empty tenant and shows
"Listening" forever, with no error anywhere. One appliance, one token, on both boards.

## 5. Make it survive the power toggle

The master toggle is a hard cut. Either enable the overlay read-only root
(`sudo raspi-config` → Performance → Overlay File System) once everything is set up, or
on a Pi 4 boot from a USB SSD. Remember to disable the overlay before the next
`provision.sh`.

## 6. Seeing it without hardware

`display/sim/run.sh` builds the same `src/ui.cpp` into an 800×480 SDL window on a
desktop, fed by the live cloud (needs SDL2 + libcurl dev packages, and
`src/secrets.h` copied from its `.example` with the token). Mouse = touch, drag to page
the mosaic. This is where the UI is developed; the Pi build only swaps the platform
layer.

## Known gaps

- The knob-toast, re-check and "ear down" overlays are present in the UI but the
  UART bridge that drives them in the LVGL app is not wired up yet (the frame codec
  is `display/src/uart_link.h`; the ESP32 side already sends). They stay dormant until
  that lands.
- The accent colour is fixed rather than sampled from the cover.
