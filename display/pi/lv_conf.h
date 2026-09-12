/* lv_conf.h for the Pi build (console-boot, no compositor). Mirrors ../sim/lv_conf.h
 * — same color depth, allocator, and font scale so the Pi renders like the sim — but
 * swaps the SDL window backend for the Linux DRM/fbdev display + evdev touch drivers.
 * Keep the font/color options in sync with ../sim/lv_conf.h. */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Match the panel + sim: RGB565. */
#define LV_COLOR_DEPTH 16

/* Pi has libc; cover art is large — use the C stdlib allocator (same as the sim). */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

/* Linux display + input backends. Both display drivers are compiled in; pi/main.cpp
 * picks one at build time via NP_USE_FBDEV (default DRM). evdev needs no extra lib —
 * LVGL reads /dev/input/eventN directly. */
#define LV_USE_LINUX_DRM   1
#define LV_USE_LINUX_FBDEV 1
#define LV_USE_EVDEV       1

/* Fonts: the 10..44 set mirrors ../sim/lv_conf.h (redesign type scale). */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_44 1

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#endif /* LV_CONF_H */
