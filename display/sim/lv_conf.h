/* Minimal lv_conf.h for the desktop SDL simulator. Only overrides; lv_conf_internal.h
 * fills every other option with its default. Keep the LV_* options here in sync with the
 * firmware's build flags in ../platformio.ini so the sim renders like the device. */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Match the device panel: RGB565. */
#define LV_COLOR_DEPTH 16

/* Desktop: use the C stdlib allocator (demo cover art is large). */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

/* SDL window backend (the whole point of the sim). */
#define LV_USE_SDL 1

/* Fonts: the 10..44 set mirrors ../platformio.ini (redesign type scale);
 * 22/28/40 stay for lv_demo_music. */
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

/* Music demo (proof the sim works; swap for our build_ui() later). */
#define LV_USE_DEMO_MUSIC 1
#define LV_DEMO_MUSIC_LARGE 1
#define LV_DEMO_MUSIC_LANDSCAPE 1
#define LV_DEMO_MUSIC_AUTO_PLAY 1

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#endif /* LV_CONF_H */
