#!/usr/bin/env bash
# Bring up BOTH Board-2 displays side by side for look comparison:
#   left  = this native-LVGL sim (the candidate)
#   right = the Chromium kiosk from ~/Code/nowplaying-pi (Chrome = source of truth)
# Idempotent: re-run any time; it rebuilds the sim and relaunches both windows.
#
# Prereqs (see ~/Code/nowplaying-project/CLAUDE.md):
#   - the cloud running:  cd ~/Code/nowplaying-cloud && php artisan serve --host=0.0.0.0 --port=8000
#                         (and: php artisan queue:work --queue=database --tries=1)
#   - SDL2 + libcurl (sim), chromium, and Hyprland (for auto-tiling via hyprctl; optional)
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
DISPLAY_REPO="$(cd "$HERE/.." && pwd)"
KIOSK_REPO="${KIOSK_REPO:-$HOME/Code/nowplaying-pi}"
KIOSK_PORT=8099
CHROME_PROFILE=/tmp/np-compare-chrome

# --- 0. cloud reachable? (warn only — both apps still open, just show 'idle') ------------
tok=$(grep -oP 'DEVICE_TOKEN\s+"\K[^"]+' "$DISPLAY_REPO/src/secrets.h" 2>/dev/null || true)
base=$(grep -oP 'BASE_URL\s+"\K[^"]+'   "$DISPLAY_REPO/src/secrets.h" 2>/dev/null || true)
if [ -z "$base" ] || ! curl -sf -m3 -H "Authorization: Bearer $tok" "$base/nowplaying" >/dev/null 2>&1; then
  echo "!! Cloud not reachable at ${base:-<no secrets.h>} — start it first:"
  echo "   cd ~/Code/nowplaying-cloud && php artisan serve --host=0.0.0.0 --port=8000"
  echo "   cd ~/Code/nowplaying-cloud && php artisan queue:work --queue=database --tries=1"
  echo "   (continuing anyway; the displays will just show 'idle')"
fi

# --- 1. LVGL sim: build + (re)launch in the background -----------------------------------
[ -d "$HERE/build" ] || cmake -S "$HERE" -B "$HERE/build"
cmake --build "$HERE/build" -j || { echo "sim build failed"; exit 1; }
pkill -x sim 2>/dev/null || true; sleep 1
( "$HERE/build/sim" ) >/tmp/np-sim.log 2>&1 &
echo "sim -> /tmp/np-sim.log"

# --- 2. Chromium kiosk: serve nowplaying-pi + open an app window -------------------------
if [ -f "$KIOSK_REPO/index.html" ]; then
  pkill -f "http.server $KIOSK_PORT" 2>/dev/null || true
  ( cd "$KIOSK_REPO" && python3 -m http.server "$KIOSK_PORT" ) >/tmp/np-kiosk-http.log 2>&1 &
  sleep 1
  pkill -f "user-data-dir=$CHROME_PROFILE" 2>/dev/null || true; sleep 1
  chromium --app="http://localhost:$KIOSK_PORT/index.html" --class=npkiosk \
    --user-data-dir="$CHROME_PROFILE" --window-size=800,480 --ozone-platform=wayland \
    >/tmp/np-kiosk-chrome.log 2>&1 &
  echo "kiosk -> http://localhost:$KIOSK_PORT (log /tmp/np-kiosk-chrome.log)"
else
  echo "!! kiosk repo not found at $KIOSK_REPO (set KIOSK_REPO=...); skipping the Chrome side"
fi

# --- 3. tile side by side (best-effort; Hyprland only) ----------------------------------
if command -v hyprctl >/dev/null && command -v python3 >/dev/null; then
  sleep 6
  addrs=$(hyprctl clients -j 2>/dev/null | python3 -c '
import json,sys
sim=kiosk=""
for c in json.load(sys.stdin):
    if c["title"]=="LVGL Simulator": sim=c["address"]
    elif c["class"].startswith("chrome") or c["title"]=="Now Playing": kiosk=c["address"]
print(sim, kiosk)')
  read -r SIM K <<<"$addrs"
  [ -n "$SIM" ] && hyprctl --batch "dispatch setfloating address:$SIM ; dispatch resizewindowpixel exact 533 320,address:$SIM ; dispatch movewindowpixel exact 120 300,address:$SIM ; dispatch alterzorder top,address:$SIM" >/dev/null 2>&1
  [ -n "$K" ]   && hyprctl --batch "dispatch setfloating address:$K ; dispatch resizewindowpixel exact 800 480,address:$K ; dispatch movewindowpixel exact 700 220,address:$K ; dispatch alterzorder top,address:$K" >/dev/null 2>&1
  echo "tiled: sim (left) + kiosk (right)"
else
  echo "(no hyprctl — windows opened un-tiled; move them yourself)"
fi
