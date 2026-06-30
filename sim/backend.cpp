// Desktop real-data backend (see backend.h). libcurl + stb_image + nlohmann/json.
#include "backend.h"
#include "ui.h"
#include "ui_platform.h"
#include "secrets.h"        // BASE_URL, DEVICE_TOKEN (../src; gitignored)
#include <curl/curl.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "json.hpp"
using json = nlohmann::json;

static std::string base_url() { const char *e = getenv("NP_BASE_URL"); return e ? e : BASE_URL; }
static std::string token()    { const char *e = getenv("NP_TOKEN");    return e ? e : DEVICE_TOKEN; }

// ---- HTTP (libcurl) ---------------------------------------------------------
static size_t to_str(void *p, size_t s, size_t n, void *u) {
  ((std::string *)u)->append((char *)p, s * n); return s * n;
}
static size_t to_vec(void *p, size_t s, size_t n, void *u) {
  auto *v = (std::vector<uint8_t> *)u; uint8_t *d = (uint8_t *)p; v->insert(v->end(), d, d + s * n); return s * n;
}
static bool http_get(const std::string &url, std::string &out, bool auth) {
  CURL *c = curl_easy_init(); if (!c) return false;
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_str);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 8L);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  struct curl_slist *h = nullptr;
  std::string a;
  if (auth) { a = "Authorization: Bearer " + token(); h = curl_slist_append(h, a.c_str()); curl_easy_setopt(c, CURLOPT_HTTPHEADER, h); }
  CURLcode r = curl_easy_perform(c);
  long code = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  if (h) curl_slist_free_all(h);
  curl_easy_cleanup(c);
  return r == CURLE_OK && code >= 200 && code < 300;
}
static bool http_get_bytes(const std::string &url, std::vector<uint8_t> &out) {
  CURL *c = curl_easy_init(); if (!c) return false;
  curl_easy_setopt(c, CURLOPT_URL, url.c_str());
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, to_vec);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  CURLcode r = curl_easy_perform(c);
  long code = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(c);
  return r == CURLE_OK && code >= 200 && code < 300 && !out.empty();
}

// ---- decode -> RGB565 (malloc'd; ui_free==free owns it), optional downscale --
static uint16_t *decode_rgb565(const std::vector<uint8_t> &bytes, int &ow, int &oh, int maxdim) {
  int w, h, n;
  unsigned char *px = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 3);
  if (!px) return nullptr;
  int dw = w, dh = h;
  if (maxdim > 0 && (w > maxdim || h > maxdim)) {
    float s = (float)maxdim / (w > h ? w : h);
    dw = (int)(w * s); dh = (int)(h * s); if (dw < 1) dw = 1; if (dh < 1) dh = 1;
  }
  uint16_t *buf = (uint16_t *)malloc((size_t)dw * dh * 2);
  if (buf) {
    for (int y = 0; y < dh; y++) {
      int sy = y * h / dh;
      for (int x = 0; x < dw; x++) {
        int sx = x * w / dw;
        unsigned char *p = px + (sy * w + sx) * 3;
        buf[y * dw + x] = ((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3);
      }
    }
  }
  stbi_image_free(px);
  ow = dw; oh = dh; return buf;
}

// ---- json helpers (null-safe) ----------------------------------------------
static void jstr(const json &j, const char *k, char *dst, size_t n) {
  dst[0] = 0;
  if (j.contains(k) && j[k].is_string()) cpystr(dst, j[k].get<std::string>().c_str(), n);
}

// ---- state ------------------------------------------------------------------
static NowPlaying g_np;                          // last poll
static std::string g_cover_url;                  // cover currently bound
static std::string g_slot_url[MOSAIC_PER_PAGE];  // tile url applied per visible slot
static int g_slot_page = -1;
static uint32_t g_last_poll = 0;

#define COVER_MAXDIM 400
#define TILE_MAXDIM  160
#define ALBUM_MAXDIM 180

static void parse_np(const std::string &body) {
  memset(&g_np, 0, sizeof g_np);
  try {
    json j = json::parse(body);
    jstr(j, "state", g_np.state, sizeof g_np.state);
    if (std::string(g_np.state) == "playing") {
      jstr(j, "track_id", g_np.track_id, sizeof g_np.track_id);
      jstr(j, "title",    g_np.title,    sizeof g_np.title);
      jstr(j, "artist",   g_np.artist,   sizeof g_np.artist);
      jstr(j, "album",    g_np.album,    sizeof g_np.album);
      jstr(j, "cover_url",g_np.cover_url, sizeof g_np.cover_url);
    }
    if (j.contains("duration_s") && j["duration_s"].is_number()) { g_np.has_duration = true; g_np.duration_s = j["duration_s"].get<float>(); }
    if (j.contains("position_s") && j["position_s"].is_number()) { g_np.has_position = true; g_np.position_s = j["position_s"].get<float>(); }
    g_np.lyrics_available = j.value("lyrics_available", false);
    if (j.contains("recent") && j["recent"].is_array()) {
      int cnt = 0;
      for (auto &t : j["recent"]) {
        if (cnt >= NP_RECENT_MAX) break;
        jstr(t, "id",        g_np.recent[cnt].id,        sizeof g_np.recent[cnt].id);
        jstr(t, "cover_url", g_np.recent[cnt].cover_url, sizeof g_np.recent[cnt].cover_url);
        cnt++;
      }
      g_np.recent_count = (uint8_t)cnt;
    }
  } catch (...) { memset(&g_np, 0, sizeof g_np); strcpy(g_np.state, "idle"); }
  g_np.received_ms = ui_millis();
}

static void handle_cover() {
  std::string want = np_is_playing(g_np) ? g_np.cover_url : "";
  if (want == g_cover_url) return;
  g_cover_url = want;
  CoverMsg m; memset(&m, 0, sizeof m);
  if (!want.empty()) {
    std::vector<uint8_t> bytes; int w, h;
    if (http_get_bytes(want, bytes)) { uint16_t *b = decode_rgb565(bytes, w, h, COVER_MAXDIM); if (b) { m.buf = b; m.w = (uint16_t)w; m.h = (uint16_t)h; m.ok = true; } }
  }
  bind_cover(m);   // ok==false -> placeholder
}

// Reconcile the visible mosaic page against g_np.recent. Cheap each tick; only fetches a
// slot whose desired url changed (so a page swipe reloads promptly without polling /nowplaying).
static void reconcile_tiles() {
  // Mirror the device's service_mosaic gating (main.cpp): when the mosaic isn't the shown
  // card the UI has freed/hidden the tile_img buffers, so forget the per-slot urls — else
  // the dedup below thinks they're still loaded and never re-sends the art when the mosaic
  // returns, leaving every tile a placeholder.
  if (!g_show_mosaic) { for (int s = 0; s < MOSAIC_PER_PAGE; s++) g_slot_url[s].clear(); g_slot_page = -1; return; }
  int page = g_mosaic_page;
  if (page != g_slot_page) { g_slot_page = page; for (int s = 0; s < MOSAIC_PER_PAGE; s++) g_slot_url[s].clear(); }
  for (int s = 0; s < MOSAIC_PER_PAGE; s++) {
    int idx = page * MOSAIC_PER_PAGE + s;
    std::string want = (idx < g_np.recent_count) ? g_np.recent[idx].cover_url : "";
    if (want == g_slot_url[s]) continue;
    g_slot_url[s] = want;
    TileMsg m; memset(&m, 0, sizeof m); m.slot = (uint8_t)s;
    if (!want.empty()) {
      std::vector<uint8_t> bytes; int w, h;
      if (http_get_bytes(want, bytes)) { uint16_t *b = decode_rgb565(bytes, w, h, TILE_MAXDIM); if (b) { m.buf = b; m.w = (uint16_t)w; m.h = (uint16_t)h; m.ok = true; } }
    }
    ui_apply_tile(m);   // ok==false clears the slot to its placeholder
  }
}

void backend_init(void) { curl_global_init(CURL_GLOBAL_DEFAULT); }

void backend_tick(void) {
  uint32_t now = ui_millis();
  if (g_last_poll == 0 || now - g_last_poll >= 3000) {
    g_last_poll = now;
    std::string body;
    if (http_get(base_url() + "/nowplaying", body, true)) {
      parse_np(body);
      render_now_playing(g_np);   // picks card, populates mosaic ids, re-anchors progress
      handle_cover();
    }
  }
  reconcile_tiles();
}

void backend_request_album(const char *id) {
  std::string body;
  if (!http_get(base_url() + "/album/" + id, body, true)) return;
  Album a; memset(&a, 0, sizeof a);
  cpystr(a.id, id, sizeof a.id);
  try {
    json j = json::parse(body);
    if (j.contains("tracks") && j["tracks"].is_array() && !j["tracks"].empty()) {
      a.ok = true;
      jstr(j, "title",     a.title,     sizeof a.title);
      jstr(j, "artist",    a.artist,    sizeof a.artist);
      jstr(j, "cover_url", a.cover_url, sizeof a.cover_url);
      int cnt = 0;
      for (auto &t : j["tracks"]) {
        if (cnt >= ALBUM_TRACKS_MAX) break;
        a.tracks[cnt].number = (uint16_t)t.value("number", 0);
        jstr(t, "title", a.tracks[cnt].title, sizeof a.tracks[cnt].title);
        a.tracks[cnt].heard = t.value("heard", false);
        cnt++;
      }
      a.track_count = (uint8_t)cnt;
    }
  } catch (...) { a.ok = false; }
  ui_apply_album(a);
  if (a.ok && a.cover_url[0]) {
    std::vector<uint8_t> bytes; int w, h;
    if (http_get_bytes(a.cover_url, bytes)) { uint16_t *b = decode_rgb565(bytes, w, h, ALBUM_MAXDIM); if (b) { CoverMsg m{b, (uint16_t)w, (uint16_t)h, true}; bind_album_cover(m); } }
  }
}
