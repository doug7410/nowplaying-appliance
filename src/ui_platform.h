// Platform shims the UI layer (ui.cpp) needs but that differ per host. The device
// (main.cpp) backs them with Arduino/ESP-IDF; the desktop sim backs them with libc/SDL.
// This is the entire hardware surface ui.cpp depends on.
#pragma once
#include <stdint.h>
#include <stddef.h>

uint32_t ui_millis(void);                 // monotonic ms (millis / SDL_GetTicks)
void     ui_logf(const char *fmt, ...);   // printf-style log line
void     ui_free(void *p);                // free a decoded-image buffer (heap_caps_free / free)
size_t   ui_free_psram(void);             // free PSRAM bytes, for logging (0 on desktop)
void     ui_request_album(const char *id);// ask the net task to GET /album/{id} (no-op in sim)
