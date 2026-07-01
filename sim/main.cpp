// Board 2 UI desktop simulator. Opens an 800x480 SDL window (mouse = touch) and runs OUR
// UI (../src/ui.cpp) fed a canned snapshot — or lv_demo_music as a reference:
//   ./build/sim          our now-playing screen
//   ./build/sim demo     lv_demo_music (the polished reference)
//
//   cmake -S . -B build && cmake --build build -j && ./build/sim
//
// The desktop renderer paints via SDL/GPU — it CANNOT reproduce the panel's hardware
// flicker. This is for look/layout iteration only; flicker stays a hardware test.
#include "lvgl.h"
#include "demos/lv_demos.h"
#include "ui.h"
#include "ui_platform.h"
#include "backend.h"
#include <SDL2/SDL.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <ctime>

// --- UI platform shims (desktop) — declared in ui_platform.h, used by ui.cpp ---
uint32_t ui_millis(void) { return SDL_GetTicks(); }
void ui_logf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
void ui_free(void *p) { free(p); }
size_t ui_free_psram(void) { return 0; }
void ui_request_album(const char *id) { backend_request_album(id); }   // tap -> real GET /album/{id}
void ui_clock_hhmm(char *out, size_t n) {                              // system localtime, 12h "H:MM"
  time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
  int h = lt.tm_hour % 12; if (h == 0) h = 12;
  snprintf(out, n, "%d:%02d", h, lt.tm_min);
}

static uint32_t tick_cb(void) { return SDL_GetTicks(); }

// Mouse-drag -> page swipe. The device self-detects swipes in its touch read_cb and consumes
// them in ui_task (main.cpp); the SDL mouse driver does neither, so paging was untestable in
// the sim. We can't wrap the read_cb — the SDL driver locates its indev by matching read_cb ==
// sdl_mouse_read, so replacing it kills all mouse input. Instead poll the indev each frame:
// latch g_swipe_request once the press has travelled >=45px horizontally (during the drag, so
// it's set before the release's CLICKED dispatches and tile_tap_cb's guard suppresses the tap),
// then consume it on release the same way ui_task does. SWIPE_MIN_PX matches the device (45).
static void swipe_tick(lv_indev_t *indev) {
  static bool down = false;
  static int x0 = 0, y0 = 0;
  lv_point_t p; lv_indev_get_point(indev, &p);
  if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
    if (!down) { down = true; x0 = p.x; y0 = p.y; g_swipe_request = 0; }
    int dh = p.x - x0, dv = p.y - y0;
    // Natural paging: drag content left (dh<0) -> NEXT page, drag right -> PREV.
    if (!g_swipe_request && abs(dh) >= 45 && abs(dh) > abs(dv))
      g_swipe_request = (dh < 0) ? +1 : -1;
  } else if (down) {
    down = false;
    if (g_swipe_request) {                          // consume, mirroring ui_task's swipe handler
      int req = g_swipe_request; g_swipe_request = 0;
      if (g_album_open) close_album();
      else if (card_idle && !lv_obj_has_flag(card_idle, LV_OBJ_FLAG_HIDDEN))
        mosaic_set_page((int)g_mosaic_page + req);
    }
  }
}

int main(int argc, char **argv) {
  lv_init();
  lv_tick_set_cb(tick_cb);
  lv_sdl_window_create(800, 480);
  lv_indev_t *mouse = lv_sdl_mouse_create();

  bool demo = (argc > 1 && strcmp(argv[1], "demo") == 0);
  if (demo) {
    lv_demo_music();
  } else {
    build_ui();
    backend_init();   // real cloud data -> the shared UI (see backend.cpp)
  }

  for (;;) {
    uint32_t t = lv_timer_handler();
    if (!demo) {                // drive the per-tick UI updates the device's ui_task would
      backend_tick();           // poll /nowplaying (throttled) + reconcile mosaic tiles
      swipe_tick(mouse);        // mouse-drag -> mosaic paging (device does this in ui_task)
      progress_render(false);
      recheck_ui_tick();
      knob_toast_tick();
    }
    SDL_Delay(t > 5 ? 5 : t);
  }
  return 0;
}
