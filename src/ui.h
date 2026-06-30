// Board 2 UI layer public surface (defined in ui.cpp). Included by main.cpp (device,
// wires net/touch/uart to it) and the desktop sim (feeds canned data). LVGL-only types
// so it compiles on both; the hardware calls ui.cpp makes go through ui_platform.h.
#pragma once
#include <lvgl.h>
#include "now_playing.h"

// Display constants used by the UI (a few also by the net layer in main.cpp).
#define PROG_BAR_STEPS   432
#define MOSAIC_COLS      4
#define MOSAIC_ROWS      2
#define MOSAIC_PER_PAGE  (MOSAIC_COLS * MOSAIC_ROWS)
#define MOSAIC_MAX_PAGES ((NP_RECENT_MAX + MOSAIC_PER_PAGE - 1) / MOSAIC_PER_PAGE)
#define TILE_PX          150

// Net task -> UI task handoff messages (the UI receiver owns/frees buf).
struct CoverMsg { uint16_t *buf; uint16_t w, h; bool ok; };
struct TileMsg  { uint16_t *buf; uint16_t w, h; uint8_t slot; bool ok; };

// Cross-task globals defined in ui.cpp, referenced by main.cpp's net/uart/touch code.
extern volatile uint8_t  g_mosaic_page;
extern volatile bool     g_album_open;
extern volatile bool     g_show_mosaic;
extern volatile int8_t   g_swipe_request;
extern volatile bool     g_recheck_request;
extern volatile uint32_t g_recheck_optim_ms;
extern volatile uint32_t g_ear_last_ms;
extern volatile uint32_t g_knob_seq;
extern volatile uint8_t  g_knob_id;
extern volatile int16_t  g_knob_val;
extern volatile uint8_t  g_knob_flags;
extern lv_obj_t         *card_idle;

// Plumbing helper (defined in ui.cpp; net code in main.cpp also calls it).
void cpystr(char *dst, const char *src, size_t n);

// UI entry points called from main.cpp (setup / tasks / queue consumers).
void build_ui();
void show_card(lv_obj_t *which);
void render_now_playing(const NowPlaying &np);
void progress_render(bool force);
void recheck_ui_tick();
void knob_toast_tick();
void close_album(void);
void mosaic_set_page(int p);
void bind_cover(const CoverMsg &m);
void bind_album_cover(const CoverMsg &m);
void ui_apply_album(const Album &a);
void ui_apply_tile(const TileMsg &m);
