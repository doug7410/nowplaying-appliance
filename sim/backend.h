// Desktop real-data backend for the sim: polls the real cloud (BASE_URL/DEVICE_TOKEN from
// ../src/secrets.h, override with env NP_BASE_URL / NP_TOKEN), decodes cover/tile JPEGs, and
// feeds the SAME shared UI (render_now_playing / bind_cover / ui_apply_tile / ui_apply_album)
// the device's net_task feeds. Mirrors the device data path; UI code is untouched.
#pragma once
void backend_init(void);                       // curl global init
void backend_tick(void);                        // call each loop iter; self-throttles polling
void backend_request_album(const char *id);     // wired to ui_request_album (cover/tile tap)
