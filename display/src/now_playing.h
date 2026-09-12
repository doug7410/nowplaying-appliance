// The live `GET /nowplaying` snapshot, as a fixed-size / trivially-copyable struct.
//
// Shape = the verified live API (docs/issues/board2/README.md "Contract corrections",
// 2026-06-24), NOT the older design summaries:
//   { state, track_id, title, artist, album, cover_url, duration_s, position_s,
//     lyrics_available (bool), recent: [ { id, cover_url } ] }
// Track fields are null unless state == "playing". There is NO anchor_epoch_ms —
// position_s is recomputed server-side each request, so we stamp `received_ms`
// (local millis() at receipt) and b2-05 interpolates the progress bar from that.
//
// Fixed char buffers (no String/heap) so the net task can hand a whole snapshot to
// the UI task by value through a FreeRTOS queue with no shared ownership. b2-03+
// render straight from this struct.

#pragma once

#include <stdint.h>
#include <string.h>

struct NpRecentTile {
  char id[24];          // Apple track/album id -> GET /album/{id} on tap (b2-07)
  char cover_url[256];  // cloudinary fetch URLs run ~206 chars — don't undersize
};

// How many recent tiles we keep client-side. The cloud serves up to RecentRing::
// RECENT_MAX = 90, but the mosaic (b2-06) only ever decodes one *page* of tiles at a
// time, so the client cap just bounds how many pages you can swipe through. 24 (≈3
// pages) exercises paging without bloating the by-value snapshot we pass through the
// queue (24 * 280 B ≈ 6.7 KB) or the stacks — keep the NowPlaying instances static.
#define NP_RECENT_MAX 24

struct NowPlaying {
  char  state[12];      // "idle" | "playing" | "setup"

  // Track fields — populated only when state == "playing", else empty strings.
  char  track_id[24];
  char  title[128];
  char  artist[128];
  char  album[128];
  char  cover_url[256];

  bool  has_duration;   // duration_s / position_s are nullable in the contract
  float duration_s;     // valid only if has_duration
  bool  has_position;
  float position_s;     // valid only if has_position; re-anchored every poll

  bool  lyrics_available;

  uint8_t      recent_count;
  NpRecentTile recent[NP_RECENT_MAX];

  uint32_t received_ms; // millis() when this snapshot was received (b2-05 anchor)
};

static inline bool np_is_playing(const NowPlaying &np) {
  return strcmp(np.state, "playing") == 0;
}

// --- b2-07 album detail (GET /album/{id}) -----------------------------------
// The tap-a-cover tracklist screen. Shape = the verified live API
// (../nowplaying-cloud/app/Http/Controllers/AlbumController.php):
//   { id, title, artist, cover_url, tracks: [ { number, title, apple_id, heard } ] }
// A lookup MISS returns 200 with title/artist/cover_url=null and tracks:[] — we map
// that to `ok=false` and render "Tracklist unavailable" (exactly like the Pi card).
// Like NowPlaying, this is a fixed-size trivially-copyable struct so the net task can
// hand a whole album to the UI task by value through a queue (keep instances static).

// Albums can be long, but the detail screen only ever shows one tracklist at a time, so
// bound it: 30 rows covers the vast majority of single albums and keeps the by-value
// struct (~30 * 100 B ≈ 3 KB) off the stacks. A longer album is truncated (logged).
#define ALBUM_TRACKS_MAX 30

struct AlbumTrack {
  uint16_t number;     // track number (0 if absent)
  char     title[96];  // track title (bounded; apple_id is unused by Board 2)
  bool     heard;      // server flagged this row as heard on THIS device's recent ring
};

struct Album {
  bool    ok;            // false => miss shape (tracks:[]) -> "Tracklist unavailable"
  char    id[24];        // the requested track/album id (echoed for log/correlation)
  char    title[128];
  char    artist[128];
  char    cover_url[256];
  uint8_t track_count;
  AlbumTrack tracks[ALBUM_TRACKS_MAX];
};
