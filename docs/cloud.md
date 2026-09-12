# The cloud: recognition service

The boards do no recognition themselves. Board 1 uploads short mic windows; a Laravel
service runs them through [`songrec`](https://github.com/marin-m/SongRec) (a headless
Shazam client), derives per-appliance now-playing state, enriches it in a queue worker
(duration and tracklist from iTunes, synced lyrics from NetEase / LRCLIB, covers via
Cloudinary), and serves it back over five HTTP endpoints. One instance serves any number
of appliances; each appliance is a tenant identified by its bearer token.

> **Not yet public.** The service source is not published. This page documents the
> contract the boards are built against and what running an instance involves, so the
> rest of the build is reproducible the day it is.

## Running an instance

- PHP 8.3 + Composer, SQLite (local) or Postgres.
- `songrec` 0.7.3 on the PATH (`songrec audio-file-to-recognized-song <wav>` is the
  only call made).
- A Cloudinary account with **fetched URLs enabled** for cover-art resizing.
- Two foreground processes:

```sh
php artisan serve --host=0.0.0.0 --port=8000        # bind the LAN so the boards reach it
php artisan queue:work --queue=database --tries=1   # fills duration / lyrics / album
```

Config knobs in `.env`: `NP_FRESHNESS_TIMEOUT=90` (seconds without an ingest before the
state decays), `NP_IDLE_AFTER=3` (consecutive misses before `idle`), `NP_RECENT_MAX=90`.
The defaults fit the boards' cadences (Board 1 uploads every ~33 s, Board 2 polls every
3–5 s). Leave them.

## Mint the appliance token

```sh
php artisan device:create      # prints the token once
```

**One appliance = one device = one token.** Put the same token in Board 1's
`room_mic_uploader_secrets.h` and Board 2's `pi/np.env`. Both `/nowplaying` and the
recent mosaic are scoped to the authenticated device, so a second token is a second,
empty appliance that shows "Listening" forever.

## The contract

All endpoints except `/health` take `Authorization: Bearer <token>`. CORS is open
(`Access-Control-Allow-Origin: *`) so a browser on the LAN can also read it.

| Method + path | Caller | Purpose |
|---|---|---|
| `GET /api/health` | anyone | liveness, no auth |
| `POST /api/ingest` | Board 1 | multipart field `file`: a **16 kHz mono 16-bit PCM WAV**, 8–18 s, RMS-gated. Recognised inline. Returns `{matched, track_id, title, artist}` |
| `GET /api/nowplaying` | Board 2 | current state + track + cover + position + recent tiles |
| `GET /api/album/{id}` | Board 2 | tracklist for a recent tile, the heard track flagged |
| `GET /api/lyrics?track_id=…` | Board 1 | synced `yrc` / `lrc`; `pending: true` until the worker has run |

`GET /api/nowplaying`:

```json
{ "state": "playing",
  "track_id": "…", "title": "…", "artist": "…", "album": "…",
  "cover_url": "https://res.cloudinary.com/…",
  "duration_s": 301, "position_s": 42.0,
  "lyrics_available": true,
  "recent": [ { "id": "…", "cover_url": "…" } ] }
```

- `state` is `setup`, `playing` or `idle`, derived server-side. Track fields are null
  unless `playing`.
- `position_s` is computed at request time. Interpolate the progress bar from the
  moment the response arrived and re-snap on every poll; there is no anchor timestamp.
- `recent[]` tiles carry only `id` and `cover_url`; `id` feeds `/album/{id}`.
- The WAV upload is MIME-sniffed. A malformed header is a 422 even if the PCM decodes.
  This is the number-one real-hardware failure mode.

## A note on recognition

`songrec` talks to Shazam's unofficial endpoint. It is free and it works, and it is also
a terms-of-service and reliability risk for anything you sell: if the endpoint changes,
every fielded unit goes quiet until the cloud is patched. The design keeps all vendor
calls server-side for exactly that reason, so swapping to a licensed API (AudD, ACRCloud)
is a cloud-only change that never touches a board.
