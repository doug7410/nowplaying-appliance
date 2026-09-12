#!/usr/bin/env bash
# Provision this Pi as the Now-Playing display: the native LVGL app (pi/np-display)
# rendering straight to the panel via DRM/KMS (or fbdev) on tty1 — no browser, no
# compositor. Replaces the Chromium kiosk (../nowplaying-pi). Run ON the Pi from the
# repo dir:  ./provision.sh   (idempotent — re-run to rebuild + reconfigure).
#
# Display backend:  DISPLAY_BACKEND=drm (default) | fbdev   ./provision.sh
#   drm   = DRM/KMS, crisp, needs to win DRM master on tty1 (logind session does this).
#   fbdev = writes /dev/fb0 directly, no master fight, tears slightly. Use if drm won't init.
set -euo pipefail

APP_DIR="$(cd "$(dirname "$0")" && pwd)"
USER_NAME="$(id -un)"
BACKEND="${DISPLAY_BACKEND:-drm}"
ENV_FILE="$APP_DIR/pi/np.env"

[ -f "$ENV_FILE" ] || { echo "Missing pi/np.env — cp pi/np.env.example pi/np.env and set NP_TOKEN (the SHARED appliance token) + NP_BASE_URL."; exit 1; }

# 1) Build deps + the binary. stb_image/json are header-only (CMake downloads them).
need=""
for p in cmake g++ git pkg-config; do command -v "$p" >/dev/null || need="$need $p"; done
dpkg -s libcurl4-openssl-dev >/dev/null 2>&1 || need="$need libcurl4-openssl-dev"
dpkg -s libdrm-dev          >/dev/null 2>&1 || need="$need libdrm-dev"
if [ -n "$need" ]; then
  echo "Installing:$need"
  sudo apt-get update -qq
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $need
fi

[ -f "$APP_DIR/src/secrets.h" ] || cp "$APP_DIR/src/secrets.h.example" "$APP_DIR/src/secrets.h"  # compile-time fallback; real token comes from np.env at runtime
FBFLAG=""; [ "$BACKEND" = fbdev ] && FBFLAG="-DNP_USE_FBDEV=ON"
echo "Building np-display ($BACKEND)…"
cmake -S "$APP_DIR/pi" -B "$APP_DIR/pi/build" $FBFLAG >/dev/null
cmake --build "$APP_DIR/pi/build" --target np-display -j2   # only our target (skip lvgl_demos)
BIN="$APP_DIR/pi/build/np-display"

# 2) The display service on tty1. PAMName=login opens a logind session that owns seat0's
# active VT — that's what lets the raw DRM app acquire DRM master (the kiosk used the same
# trick for cage). Conflicts=getty@tty1 so the text console doesn't hold the VT.
sudo tee /etc/systemd/system/np-display.service >/dev/null <<EOF
[Unit]
Description=Now-Playing display (LVGL, $BACKEND)
After=network.target systemd-user-sessions.service getty@tty1.service
Conflicts=getty@tty1.service

[Service]
User=$USER_NAME
PAMName=login
TTYPath=/dev/tty1
TTYReset=yes
TTYVHangup=yes
TTYVTDisallocate=yes
StandardInput=tty-fail
StandardOutput=journal
StandardError=journal
EnvironmentFile=$ENV_FILE
WorkingDirectory=$APP_DIR
ExecStart=$BIN
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
EOF

# 3) Stop the old Chromium kiosk (it holds tty1 + DRM master) and switch over.
sudo systemctl disable --now np-kiosk.service 2>/dev/null || true
sudo systemctl daemon-reload
sudo systemctl enable np-display.service
sudo systemctl restart np-display.service
echo "Provisioned ($BACKEND). Display on tty1; logs: journalctl -u np-display -f"
