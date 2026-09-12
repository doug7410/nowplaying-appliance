#!/usr/bin/env bash
# Build and (re)launch the Board 2 UI simulator. The one-command iterate loop:
# edit code -> ./run.sh -> see it. Closes any running instance first.
# Live auto-reload on save (optional, needs `entr`):
#   ls *.c *.h | entr -r ./run.sh
set -e
cd "$(dirname "$0")"
[ -d build ] || cmake -S . -B build      # first run: configure (fetches lvgl)
pkill -f build/sim 2>/dev/null || true
cmake --build build -j
exec ./build/sim
