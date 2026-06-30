// Board 2 Pi entry point: drives the SHARED UI (../src/ui.cpp) on the real panel via
// LVGL's Linux DRM (default) or fbdev display driver + evdev touch — the same role
// ../sim/main.cpp plays with SDL. Everything below the platform layer (ui.cpp, the
// libcurl/stb_image backend.cpp, swipe paging) is reused unchanged.
//
//   cmake -S pi -B pi/build && cmake --build pi/build -j && ./pi/build/np-display
//   fbdev fallback:  cmake -S pi -B pi/build -DNP_USE_FBDEV=ON
//
// Config (host + shared token) comes from env NP_BASE_URL / NP_TOKEN (set in the
// systemd unit) — see backend.cpp; src/secrets.h is only the compile-time fallback.
#include "lvgl.h"
#include "ui.h"
#include "ui_platform.h"
#include "../sim/backend.h"   // reused verbatim; sim/ is NOT on the -I path (its lv_conf.h
                              // would shadow pi/lv_conf.h and silently disable the DRM driver)
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

#ifndef NP_DRM_CARD
#define NP_DRM_CARD "/dev/dri/card0"
#endif
#ifndef NP_FB_DEV
#define NP_FB_DEV "/dev/fb0"
#endif
#ifndef NP_TOUCH_DEV
#define NP_TOUCH_DEV "/dev/input/event0"   // QDtech MPI5001 USB touch panel
#endif

static uint32_t now_ms(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

// --- UI platform shims (Pi) — declared in ui_platform.h, used by ui.cpp ---
uint32_t ui_millis(void) { return now_ms(); }
void ui_logf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
void ui_free(void *p) { free(p); }
size_t ui_free_psram(void) { return 0; }                       // no PSRAM on the Pi
void ui_request_album(const char *id) { backend_request_album(id); }  // tap -> real GET /album/{id}

// Mouse/touch-drag -> page swipe. Identical to the sim's swipe_tick — it's pure LVGL
// indev polling (no SDL), so the evdev pointer drives it unchanged. Latch a horizontal
// drag >=45px during the press (before the release's CLICKED fires), consume on release.
static void swipe_tick(lv_indev_t *indev) {
  static bool down = false;
  static int x0 = 0, y0 = 0;
  lv_point_t p; lv_indev_get_point(indev, &p);
  if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {
    if (!down) { down = true; x0 = p.x; y0 = p.y; g_swipe_request = 0; }
    int dh = p.x - x0, dv = p.y - y0;
    if (!g_swipe_request && abs(dh) >= 45 && abs(dh) > abs(dv))
      g_swipe_request = (dh < 0) ? +1 : -1;   // drag left -> NEXT, right -> PREV
  } else if (down) {
    down = false;
    if (g_swipe_request) {
      int req = g_swipe_request; g_swipe_request = 0;
      if (g_album_open) close_album();
      else if (card_idle && !lv_obj_has_flag(card_idle, LV_OBJ_FLAG_HIDDEN))
        mosaic_set_page((int)g_mosaic_page + req);
    }
  }
}

int main(void) {
  lv_init();
  lv_tick_set_cb(now_ms);

#ifdef NP_USE_FBDEV
  lv_display_t *disp = lv_linux_fbdev_create();
  lv_linux_fbdev_set_file(disp, NP_FB_DEV);
#else
  lv_display_t *disp = lv_linux_drm_create();
  lv_linux_drm_set_file(disp, NP_DRM_CARD, -1);   // -1 = auto-pick the connected connector
#endif
  (void)disp;

  lv_indev_t *touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, NP_TOUCH_DEV);
  if (!touch) ui_logf("WARN: no touch indev on %s — display only\n", NP_TOUCH_DEV);

  build_ui();
  backend_init();   // real cloud data -> the shared UI (NP_BASE_URL / NP_TOKEN from env)

  for (;;) {
    uint32_t t = lv_timer_handler();
    backend_tick();          // poll /nowplaying (throttled) + reconcile mosaic tiles
    if (touch) swipe_tick(touch);
    progress_render(false);
    recheck_ui_tick();
    knob_toast_tick();
    usleep((t > 5 ? 5 : t) * 1000);
  }
  return 0;
}
