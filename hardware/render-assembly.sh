#!/usr/bin/env bash
# Regenerate the assembly drawings + 3D renders in each board's assembly/ dir.
# Needs kicad-cli (KiCad 9/10) + rsvg-convert.  Run from hardware/:  ./render-assembly.sh
set -euo pipefail
cd "$(dirname "$0")"
for b in carrier-board knob-board mic-board panelboard; do
  pcb="$b/$b.kicad_pcb"; out="$b/assembly"; mkdir -p "$out"
  # Placement drawings: outline + fab outlines/refdes + silkscreen. Bottom is mirrored (as you look at it).
  kicad-cli pcb export svg -l Edge.Cuts,F.Fab,F.SilkS --sp --page-size-mode 2 --exclude-drawing-sheet --black-and-white \
     --mode-single -o "$out/placement-top.svg" "$pcb"
  kicad-cli pcb export svg -l Edge.Cuts,B.Fab,B.SilkS --sp --page-size-mode 2 --exclude-drawing-sheet --black-and-white -m \
     --mode-single -o "$out/placement-bottom.svg" "$pcb"
  for s in top bottom; do rsvg-convert -b white -z 6 "$out/placement-$s.svg" -o "$out/placement-$s.png"; done
  kicad-cli pcb export pdf -l Edge.Cuts,F.Fab,F.SilkS,B.Fab,B.SilkS --sp --mode-multipage --black-and-white \
     -o "$out/placement.pdf" "$pcb"
  # 3D renders: what the populated board looks like.
  for s in top bottom; do
    kicad-cli pcb render --side $s --quality high --floor -w 2000 -h 1400 --background opaque -o "$out/render-$s.png" "$pcb"
  done
  rm -f "$out"/*.svg
done
