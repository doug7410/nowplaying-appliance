// Board 2 UI layer — extracted from main.cpp (step 1). Pure LVGL; the few hardware
// calls (millis/log/free/psram/album-request) go through ui_platform.h so this file
// compiles BOTH on the device firmware and the desktop SDL sim. Behaviour is identical
// to the pre-extraction main.cpp — this was a move, not a rewrite.
#include "ui.h"
#include "ui_platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// === Design tokens (firmware/board2/docs/ui-design-spec.md) ====================
// Colors. Muted greys are NOT separate hexes — they're COL_TEXT at a reduced text
// opacity (the spec's rgba(243,244,246,α)); see TXT_* below.
#define COL_BG      0x0A0B0E   // screen background
#define COL_TEXT    0xF3F4F6   // primary text
#define COL_TITLE   0xFBFBFD   // brightest (now-playing / album titles)
#define COL_ACCENT  0x6FC7E8   // oklch(0.76 0.145 230) ~ sky cyan
                               // ponytail: one fixed accent. Per-album hue from the decoded
                               // cover is the upgrade path — no struct field carries it.
#define COL_AMBER   0xE9B765   // mic offline / ear-down banner
#define COL_GREEN   0x5CC98C   // mic OK (oklch 0.78 0.13 150)
#define COL_PANEL   0x14161B   // mini-strip / album rail wash
#define COL_PH      0x171A1F   // placeholder square

// Text opacities = the spec's rgba alphas over COL_TEXT.
#define TXT_ELAPSED 179   // ~0.70
#define TXT_ALBUM   118   // ~0.46
#define TXT_FAINT   102   // ~0.40
#define TXT_LEAST    87   // ~0.34

// Type scale -> the 8 enabled Montserrat faces (platformio.ini / sim/lv_conf.h).
#define F_TINY    &lv_font_montserrat_10   // mini-strip eyebrow(9), HEARD(10)
#define F_EYEBROW &lv_font_montserrat_12   // "NOW PLAYING"(11), back-link(12)
#define F_SMALL   &lv_font_montserrat_14   // clock / times / pill / banner(13)
#define F_BODY    &lv_font_montserrat_16   // album / subtitle / track(15-16)
#define F_TAB     &lv_font_montserrat_18   // tabs(18), mini track(17)
#define F_H2      &lv_font_montserrat_24   // artist / album title / knob mode(24-25)
#define F_H1      &lv_font_montserrat_32   // "Listening"(30)
#define F_HERO    &lv_font_montserrat_44   // now-playing title(44)

volatile bool     g_recheck_request  = false;  // UI button -> UART task sends a RECHECK frame
volatile uint32_t g_recheck_optim_ms = 0;      // ui_millis() of the tap; drives the optimistic "LISTENING…"
volatile uint32_t g_ear_last_ms      = 0;      // ui_millis() of the last inbound HEARTBEAT (0 = never heard)
#define EAR_TIMEOUT_MS    3000    // ~3 missed 1 Hz heartbeats -> the ear (Board 1) is considered down
#define RECHECK_OPTIM_MS  12000   // hold "LISTENING…" this long after a tap (a capture+recognize budget)

// b2-13 v2 knob toast: the UART task records the latest inbound KNOB_EVENT here; the UI task
// renders it (LVGL is single-threaded). g_knob_seq bumps per event so the UI task spots a new
// one even if id/value/flags repeat (same knob turned twice the same way).
volatile uint32_t g_knob_seq   = 0;
volatile uint8_t  g_knob_id    = 0;   // 0=mode, 1=bright, 2=gain
volatile int16_t  g_knob_val   = 0;   // new accumulated level/position from Board 1
volatile uint8_t  g_knob_flags = 0;   // bit0 = press
#define KNOB_TOAST_MS     1800    // hold the knob toast this long after the last event
#define MODE_LOOK_COUNT   7       // # of looks Board 1's mode knob cycles (6 Keybeat GIFs + Mountain)
                                  // ponytail: mirrors Board 1's preset cycle; a drift only mis-labels, never crashes

static lv_obj_t *card_playing = nullptr;
lv_obj_t *card_idle    = nullptr;
static lv_obj_t *card_setup   = nullptr;
static lv_obj_t *card_listening = nullptr;   // idle/Listening screen (NOW tab when nothing plays)
static lv_obj_t *lbl_title = nullptr, *lbl_artist = nullptr, *lbl_album = nullptr;

// b2-04 cover slot on the playing card: the decoded album art (cover_img) OR a
// placeholder square (cover_ph) — exactly one visible. cover_dsc is static because
// LVGL keeps the pointer we hand it (lv_image_set_src does not copy the descriptor);
// cur_cover_buf is the PSRAM buffer currently bound, freed on the next swap.
static lv_obj_t      *cover_img = nullptr, *cover_ph = nullptr;
static lv_image_dsc_t cover_dsc;
static uint16_t      *cur_cover_buf = nullptr;
// Full-bleed blurred backdrop on the playing card (spec's blur(64px) backdrop). LVGL has no
// gaussian blur, so we box-average the decoded cover down to a tiny BG_W×BG_H buffer and let the
// anti-aliased image scaler upscale it to fill the screen — a tiny source reads as a soft blur.
// Refreshed alongside the card cover in bind_cover. Static buffer (no alloc): BG_W*BG_H*2 bytes.
#define BG_W 24
#define BG_H 15
static lv_obj_t      *cover_bg_img = nullptr;
static uint16_t       bg_small[BG_W * BG_H];
static lv_image_dsc_t bg_dsc;

// b2-05 progress bar: an lv_bar with elapsed (left) / total (right) time labels on the
// playing card. The bar advances every UI tick by interpolating LOCALLY between polls,
// then re-snaps to the server on each poll — we never integrate our own play-clock.
static lv_obj_t *prog_bar = nullptr, *lbl_elapsed = nullptr, *lbl_total = nullptr;

// UI-side mirror of the latest poll, used to interpolate the bar between snapshots.
// position_s is server-relative and recomputed every poll (no anchor), so we store it
// with the local ui_millis() at receipt and add only the elapsed-since-receipt term; a
// fresh poll overwrites both wholesale (re-snap), never accumulating across polls.
static bool     prog_playing    = false;   // current card is a playing track?
static bool     prog_has_dur    = false;   // duration known? false -> indeterminate (no fraction)
static float    prog_server_pos = 0.0f;    // position_s from the last poll
static float    prog_duration   = 0.0f;    // duration_s from the last poll (valid iff prog_has_dur)
static uint32_t prog_recv_ms    = 0;       // ui_millis() at that poll's receipt (the local anchor)
static char     prog_track_id[24] = {0};   // detect a track change -> snap, don't tween from old pos

// b2-12 mini now-playing strip on the RECENTLY screen (card_idle). The Pi showed a compact
// now-playing banner on its recently-played view while a track was playing (np_display _now_strip);
// this is that, ported. Shown ONLY while playing AND the user is on RECENTLY (apply_view); a
// floating bottom strip with a small cover, "NOW PLAYING" label, title/artist, and a thin progress
// bar. Tapping it flips back to NOW PLAYING. The cover REUSES the full card's decoded buffer
// (cover_dsc) — no second fetch/decode — so it's refreshed whenever the card cover rebinds. The
// progress bar mirrors the card bar off the same interpolation state (progress_render drives both).
static lv_obj_t *mini_strip     = nullptr;   // floating bottom container (hidden unless shown)
static lv_obj_t *mini_cover_img = nullptr;   // small cover (shares cover_dsc with the card cover)
static lv_obj_t *mini_cover_ph  = nullptr;   // placeholder square (no decoded cover yet)
static lv_obj_t *mini_title     = nullptr;   // track title (1 line, ellipsized)
static lv_obj_t *mini_artist    = nullptr;   // artist (1 line, ellipsized)
static lv_obj_t *mini_prog_bar  = nullptr;   // thin progress bar (mirrors prog_bar)

// b2-13 RE-CHECK / listening button (floating bottom of card_playing). The dot is a tri-state
// listening indicator driven by the Board 1 heartbeat (green alive / red down / grey unknown);
// the label is "RE-CHECK", flipping to "LISTENING…" for RECHECK_OPTIM_MS after a tap. recheck_ui_tick
// repaints only on a real state change — a steady (non-breathing) dot, deliberately, so it doesn't
// add to this RGB panel's whole-screen-per-write flicker. (Animated breath = a later flicker-checked step.)
static lv_obj_t *recheck_btn = nullptr;   // floating pill
static lv_obj_t *recheck_dot = nullptr;   // listening indicator
static lv_obj_t *recheck_lbl = nullptr;   // "RE-CHECK" / "LISTENING…"

static lv_obj_t *knob_toast      = nullptr;   // b2-13 v2 floating knob toast (top layer)
static lv_obj_t *knob_toast_name = nullptr;   // "BRIGHTNESS"
static lv_obj_t *knob_toast_val  = nullptr;   // "24" / "press"

// b2-13 v2 link-state cue. ear_banner: a top-layer "ear down" banner shown when the Board 1
// heartbeat goes stale (earst==2). Repaints only on a real state change (the RGB panel flickers
// the whole FB on any write — same rule as the dot).
static lv_obj_t *ear_banner  = nullptr;   // top-layer "ear down" banner

// b2-06 idle mosaic widgets + state. The grid holds exactly MOSAIC_PER_PAGE cells; a
// page just re-fills the same cells, so at most MOSAIC_PER_PAGE decoded tile buffers are
// alive (PSRAM-bounded regardless of recent[] length). tile_buf[] are the bound PSRAM
// buffers (freed on swap, UI task only). The net task fetches/decodes tiles and hands
// them over via tile_queue (a TileMsg per slot).
static lv_obj_t      *mosaic_grid  = nullptr;            // the 3x3 tile container
static lv_obj_t      *mosaic_empty = nullptr;            // "No recent tracks" (recent_count==0)
static lv_obj_t      *mosaic_dots  = nullptr;            // pagination dot row
static lv_obj_t      *dot[MOSAIC_MAX_PAGES] = {nullptr}; // the dots themselves
static lv_obj_t      *tile_cell[MOSAIC_PER_PAGE] = {nullptr};  // clickable placeholder square
static lv_obj_t      *tile_img[MOSAIC_PER_PAGE]  = {nullptr};  // decoded art (hidden until bound)
static lv_image_dsc_t tile_dsc[MOSAIC_PER_PAGE];
static uint16_t      *tile_buf[MOSAIC_PER_PAGE]  = {nullptr};
static char           ui_slot_id[MOSAIC_PER_PAGE][24] = {{0}};  // tapped-tile id, by visible slot

// Mosaic paging. ui_recent_count/ui_page_count are UI-derived from the latest snapshot;
// g_mosaic_page is the visible page, written ONLY by the UI task (swipe / dot tap) and
// read by the net task to know which slots to load. Single-byte volatile = atomic enough
// across cores on this MCU.
static uint8_t          ui_recent_count = 0;
static uint8_t          ui_page_count   = 1;
static NowPlaying       ui_np;                 // UI mirror of the last snapshot (for re-paging between polls)
volatile uint8_t g_mosaic_page  = 0;

// b2-07 album detail. A 4th card, shown by NAVIGATION (a tap) rather than by the poll's
// server-derived state. The poll keeps running underneath: render_now_playing still parses
// every snapshot and records the card it *would* show in g_active_card, but skips the actual
// show_card while g_album_open so the album screen isn't clobbered every ~4 s. Back clears
// the flag and restores g_active_card. ui_track_id is the now-playing track id (set each
// playing snapshot) so a tap on the card cover knows which album to open.
static lv_obj_t      *card_album        = nullptr;  // the detail screen
static lv_obj_t      *album_cover_img   = nullptr;  // header album art (decoded)
static lv_obj_t      *album_cover_ph    = nullptr;  // header placeholder square
static lv_obj_t      *lbl_album_title   = nullptr;  // album title (header)
static lv_obj_t      *lbl_album_artist  = nullptr;  // album artist (header)
static lv_obj_t      *lbl_album_meta    = nullptr;  // "{n} TRACKS · {dur}" under the artist
static lv_obj_t      *album_list        = nullptr;  // scrollable tracklist container
static lv_obj_t      *lbl_album_loading = nullptr;  // "Loading…" until the fetch lands
static lv_obj_t      *lbl_album_unavail = nullptr;  // "Tracklist unavailable" (miss path)
static lv_image_dsc_t album_cover_dsc;
static uint16_t      *cur_album_cover_buf = nullptr;          // bound header-art buffer (UI frees on swap)
static char           ui_track_id[24] = {0};                  // current playing track id (for the cover tap)
static lv_obj_t      *g_active_card  = nullptr;               // card the last snapshot would show (restored on back)
volatile bool  g_album_open   = false;                 // album detail visible? gates the poll's show_card

// b2-09 two-tab header (NOW PLAYING | RECENTLY). The user-chosen view persists while playing —
// like the Pi's np_view — so the ~4 s poll won't yank you off the mosaic. While the mosaic is
// the active card (state idle, OR playing + VIEW_RECENT) g_show_mosaic tells the net task to keep
// the visible page's tiles loaded (service_mosaic), exactly as the idle state did before.
enum { VIEW_NOW = 0, VIEW_RECENT = 1 };
volatile uint8_t g_user_view   = VIEW_NOW;   // tab selection; honoured only while playing
volatile bool    g_show_mosaic = false;      // net task: load tiles whenever the mosaic shows
// One header per card it sits on (separate full-screen panels): [0]=card_playing,
// [1]=card_idle (Recently mosaic), [2]=card_listening. style_tabs recolors all copies in sync.
#define TAB_HDRS 3
static lv_obj_t *tab_now_cell[TAB_HDRS] = {nullptr};
static lv_obj_t *tab_rec_cell[TAB_HDRS] = {nullptr};
static lv_obj_t *tab_now_lbl[TAB_HDRS]  = {nullptr};
static lv_obj_t *tab_rec_lbl[TAB_HDRS]  = {nullptr};
static lv_obj_t *tab_status_dot[TAB_HDRS] = {nullptr};   // right-side mic/ear dot per header

volatile int8_t g_swipe_request = 0;            // -1 prev page, +1 next page, 0 none

// A full-screen panel (one per card state), borderless, non-scrolling.
static lv_obj_t *make_card(uint32_t bg) {
  lv_obj_t *p = lv_obj_create(lv_screen_active());
  lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
  lv_obj_center(p);
  lv_obj_set_style_bg_color(p, lv_color_hex(bg), 0);
  lv_obj_set_style_border_width(p, 0, 0);
  lv_obj_set_style_radius(p, 0, 0);
  lv_obj_set_style_pad_all(p, 24, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  return p;
}

// A wrapping, centered label that fills the card width.
static lv_obj_t *card_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int pad_top) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_style_pad_top(l, pad_top, 0);
  lv_label_set_text(l, "");
  return l;
}

// A LEFT-aligned label (the redesign's now-playing / album text). opa is the spec's text
// alpha over COL_TEXT (LV_OPA_COVER for full). Wraps at the parent width.
static lv_obj_t *meta_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            lv_opa_t opa, int pad_top) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_style_text_opa(l, opa, 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_style_pad_top(l, pad_top, 0);
  lv_label_set_text(l, "");
  return l;
}

// A small horizontal equalizer ornament (N accent bars) — the decorative "playing" cue
// next to NOW PLAYING eyebrows and mini strips. Static bars (no animation): the RGB panel
// flickers on every redraw, so an animated equalizer is a later flicker-checked step.
static lv_obj_t *make_equalizer(lv_obj_t *parent, int bars, int bar_w, int gap,
                                const int *heights, uint32_t color) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_column(row, gap, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  for (int i = 0; i < bars; i++) {
    lv_obj_t *b = lv_obj_create(row);
    lv_obj_set_size(b, bar_w, heights[i]);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_radius(b, bar_w / 2, 0);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_EVENT_BUBBLE);
  }
  return row;
}

// ---------------------------------------------------------------------------
// b2-06 idle mosaic — grid layout, paging, and tap (all UI task).
// ---------------------------------------------------------------------------
static void open_album(const char *id);                     // b2-07 nav -> album detail
void close_album(void);                              // b2-07 back -> prior card
static void build_tab_header(lv_obj_t *parent, int idx);    // b2-09 two-tab header
static void apply_view(const NowPlaying &np);               // b2-09 pick + show the active card

// Apply the current page (g_mosaic_page) to the widgets WITHOUT touching the decoded
// art: set each slot's tappable id from recent[], update the dot row, and toggle the
// empty-state label. Art is filled separately by the net task (ui_apply_tile). Cheap +
// idempotent — called on a fresh idle snapshot and on every page change.
static void mosaic_apply_page() {
  uint8_t page = g_mosaic_page;
  bool empty = (ui_recent_count == 0);
  if (mosaic_empty) (empty ? lv_obj_remove_flag : lv_obj_add_flag)(mosaic_empty, LV_OBJ_FLAG_HIDDEN);
  if (mosaic_grid)  (empty ? lv_obj_add_flag : lv_obj_remove_flag)(mosaic_grid,  LV_OBJ_FLAG_HIDDEN);
  if (mosaic_dots)  (empty ? lv_obj_add_flag : lv_obj_remove_flag)(mosaic_dots,  LV_OBJ_FLAG_HIDDEN);

  for (int slot = 0; slot < MOSAIC_PER_PAGE; slot++) {
    int idx = page * MOSAIC_PER_PAGE + slot;
    if (idx < ui_recent_count) {
      cpystr(ui_slot_id[slot], ui_np.recent[idx].id, sizeof(ui_slot_id[slot]));
      lv_obj_set_style_opa(tile_cell[slot], LV_OPA_COVER, 0);
      lv_obj_add_flag(tile_cell[slot], LV_OBJ_FLAG_CLICKABLE);
    } else {
      // Empty slot on a partial last page: keep the cell in the flex layout (don't HIDE it)
      // so the grid stays a fixed 3×3 and the real tiles keep their columns — that left-
      // aligns a partial bottom row. Just make it invisible + untappable.
      ui_slot_id[slot][0] = '\0';
      lv_obj_set_style_opa(tile_cell[slot], LV_OPA_TRANSP, 0);
      lv_obj_remove_flag(tile_cell[slot], LV_OBJ_FLAG_CLICKABLE);
    }
  }

  // Dots: show one per page, highlight the current as a wide pill. Hidden when a single page.
  for (int p = 0; p < MOSAIC_MAX_PAGES; p++) {
    if (!dot[p]) continue;
    if (p < ui_page_count && ui_page_count > 1) {
      lv_obj_remove_flag(dot[p], LV_OBJ_FLAG_HIDDEN);
      bool on = (p == page);
      lv_obj_set_width(dot[p], on ? 18 : 5);                       // active = 18x5 pill, else 5x5
      lv_obj_set_style_bg_opa(dot[p], on ? 217 : 72, 0);           // ~0.85 / ~0.28
    } else {
      lv_obj_add_flag(dot[p], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// Unbind + free every decoded tile buffer back to the placeholder (UI task owns these).
// Called on a page change (stale art must not linger on the reused cells) and when we
// leave idle (reclaim PSRAM; the net task reloads on return). Idempotent.
static void mosaic_clear_images() {
  for (int slot = 0; slot < MOSAIC_PER_PAGE; slot++) {
    if (tile_img[slot]) {
      lv_image_set_src(tile_img[slot], NULL);
      lv_obj_add_flag(tile_img[slot], LV_OBJ_FLAG_HIDDEN);
    }
    if (tile_buf[slot]) { ui_free(tile_buf[slot]); tile_buf[slot] = nullptr; }
  }
}

// Jump to page p (clamped). Clears the old page's art so the net task re-fills the cells
// for the new page; refreshes ids + dots immediately so a tap right after the swipe is
// correct even before the art arrives.
void mosaic_set_page(int p) {
  if (ui_page_count < 1) ui_page_count = 1;
  if (p < 0) p = 0;
  if (p > ui_page_count - 1) p = ui_page_count - 1;
  if ((uint8_t)p == g_mosaic_page) return;
  g_mosaic_page = (uint8_t)p;
  mosaic_clear_images();
  mosaic_apply_page();
  ui_logf("[mosaic] page -> %d/%d\n", (int)g_mosaic_page + 1, ui_page_count);
}

// Tap a tile -> open album detail for its Apple id (b2-07). Ignores empty slots (a page's
// trailing blanks have no id).
static void tile_tap_cb(lv_event_t *e) {
  // Suppress the tap if this release was actually a swipe. LVGL fires CLICKED on the
  // press-START tile even when the finger dragged across it (our cells are non-scrollable,
  // so motion doesn't cancel the click). lv_touch_read_cb sets g_swipe_request on the same
  // release read, synchronously before LVGL dispatches this CLICKED — so a non-zero flag
  // here means "that was a page swipe, not a tap." (ui_task clears it right after.)
  if (g_swipe_request) return;
  int slot = (int)(intptr_t)lv_event_get_user_data(e);
  if (slot < 0 || slot >= MOSAIC_PER_PAGE) return;
  if (ui_slot_id[slot][0] == '\0') return;
  ui_logf("[mosaic] tap slot %d (page %d) -> album id %s\n",
                slot, (int)g_mosaic_page + 1, ui_slot_id[slot]);
  open_album(ui_slot_id[slot]);
}

// Tap the now-playing card cover -> open the current track's album (b2-07). Bound to both
// the decoded cover image and its placeholder square (exactly one is visible at a time).
static void cover_tap_cb(lv_event_t *e) {
  if (g_swipe_request) return;               // a swipe over the cover isn't a tap
  if (ui_track_id[0] == '\0') return;        // nothing playing -> nothing to open
  ui_logf("[card] cover tap -> album id %s\n", ui_track_id);
  open_album(ui_track_id);
}

// Tap a pagination dot -> jump straight to that page.
static void dot_tap_cb(lv_event_t *e) {
  mosaic_set_page((int)(intptr_t)lv_event_get_user_data(e));
}

// Swipe paging is handled by self-detection in lv_touch_read_cb / ui_task (see
// g_swipe_request) rather than LVGL's LV_EVENT_GESTURE — the accumulator was unreliable on
// this GT911. The GESTURE_BUBBLE flags on the tiles are now harmless no-ops kept for clarity.
// Snap paging, not free scroll: each page settle redraws the grid once, keeping the RGB-panel
// tearing (b2-11) to a single flick per page.

// Refresh the mosaic from a fresh idle snapshot: mirror it for between-poll re-paging,
// recompute the page count, clamp the current page, then apply ids/dots. Art follows
// from the net task.
static void mosaic_on_snapshot(const NowPlaying &np) {
  ui_np = np;                                  // retain for swipe re-paging between polls
  ui_recent_count = np.recent_count;
  ui_page_count = (ui_recent_count + MOSAIC_PER_PAGE - 1) / MOSAIC_PER_PAGE;
  if (ui_page_count < 1) ui_page_count = 1;
  if (g_mosaic_page > ui_page_count - 1) { g_mosaic_page = ui_page_count - 1; mosaic_clear_images(); }
  mosaic_apply_page();
}

// b2-07 back affordance: tap the ‹ button to leave album detail. Suppressed if this
// release was actually a swipe (the swipe handler in ui_task does the close instead) —
// same guard the mosaic tiles use, so a swipe-back doesn't also fire a stray button tap.
static void back_tap_cb(lv_event_t *e) {
  if (g_swipe_request) return;
  close_album();
}

// Reset the album card to its LOADING state: header text cleared, cover -> placeholder,
// tracklist emptied, "Loading…" shown. Called the instant a tap opens the card, before the
// fetch lands, so the screen is never blank or stale-from-the-last-album. UI task only.
static void album_show_loading() {
  if (lbl_album_title)  lv_label_set_text(lbl_album_title, "");
  if (lbl_album_artist) lv_label_set_text(lbl_album_artist, "");
  if (lbl_album_meta)   lv_label_set_text(lbl_album_meta, "");
  if (album_list)        { lv_obj_clean(album_list); lv_obj_add_flag(album_list, LV_OBJ_FLAG_HIDDEN); }
  if (lbl_album_unavail) lv_obj_add_flag(lbl_album_unavail, LV_OBJ_FLAG_HIDDEN);
  if (lbl_album_loading) lv_obj_remove_flag(lbl_album_loading, LV_OBJ_FLAG_HIDDEN);
  // Drop any previously-bound header art back to the placeholder (and reclaim its PSRAM).
  if (album_cover_img) lv_image_set_src(album_cover_img, NULL);
  if (cur_album_cover_buf) { ui_free(cur_album_cover_buf); cur_album_cover_buf = nullptr; }
  if (album_cover_img) lv_obj_add_flag(album_cover_img, LV_OBJ_FLAG_HIDDEN);
  if (album_cover_ph)  lv_obj_remove_flag(album_cover_ph, LV_OBJ_FLAG_HIDDEN);
}

// b2-12 mini-strip cover thumbnail size (redesign: 44px, fits the 66px strip).
#define MINI_THUMB 44

// A tap anywhere on the mini strip flips to the full NOW PLAYING card — the Pi's in_strip
// behaviour. Same swipe guard as the tabs (a page-swipe that grazes the strip mustn't tap).
static void mini_strip_cb(lv_event_t *e) {
  if (g_swipe_request) return;
  g_user_view = VIEW_NOW;
  apply_view(ui_np);
  ui_logf("[mini] strip tap -> NOW PLAYING\n");
}

// Build the compact now-playing strip on card_idle: [44px cover | NOW PLAYING eyebrow + "title —
// artist" | 4-bar equalizer]. FLOATING so it's out of the mosaic's flex flow and pinned full-bleed
// to the card bottom. Hidden until apply_view shows it (playing + RECENTLY view).
static void build_mini_strip(lv_obj_t *parent) {
  mini_strip = lv_obj_create(parent);
  lv_obj_set_width(mini_strip, LV_PCT(100));
  lv_obj_set_height(mini_strip, 66);
  lv_obj_add_flag(mini_strip, LV_OBJ_FLAG_FLOATING);          // ignored by the card's flex layout
  lv_obj_align(mini_strip, LV_ALIGN_BOTTOM_MID, 0, 78);       // +pad_bottom -> flush at the card's bottom edge
  lv_obj_set_style_bg_color(mini_strip, lv_color_hex(COL_PANEL), 0);
  lv_obj_set_style_bg_opa(mini_strip, 235, 0);
  lv_obj_set_style_border_width(mini_strip, 1, 0);
  lv_obj_set_style_border_side(mini_strip, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(mini_strip, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_opa(mini_strip, 20, 0);
  lv_obj_set_style_radius(mini_strip, 0, 0);
  lv_obj_set_style_pad_ver(mini_strip, 11, 0);
  lv_obj_set_style_pad_hor(mini_strip, 40, 0);
  lv_obj_set_style_pad_column(mini_strip, 14, 0);
  lv_obj_clear_flag(mini_strip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(mini_strip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(mini_strip, LV_OBJ_FLAG_GESTURE_BUBBLE);    // a swipe still pages the mosaic
  lv_obj_add_event_cb(mini_strip, mini_strip_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_set_flex_flow(mini_strip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(mini_strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(mini_strip, LV_OBJ_FLAG_HIDDEN);

  mini_cover_img = lv_image_create(mini_strip);
  lv_obj_set_size(mini_cover_img, MINI_THUMB, MINI_THUMB);
  lv_image_set_inner_align(mini_cover_img, LV_IMAGE_ALIGN_COVER);   // scale the card-sized art down
  lv_obj_set_style_radius(mini_cover_img, 6, 0);
  lv_obj_set_style_clip_corner(mini_cover_img, true, 0);
  lv_obj_add_flag(mini_cover_img, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(mini_cover_img, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(mini_cover_img, LV_OBJ_FLAG_GESTURE_BUBBLE);

  mini_cover_ph = lv_obj_create(mini_strip);
  lv_obj_set_size(mini_cover_ph, MINI_THUMB, MINI_THUMB);
  lv_obj_set_style_bg_color(mini_cover_ph, lv_color_hex(COL_PH), 0);
  lv_obj_set_style_border_width(mini_cover_ph, 0, 0);
  lv_obj_set_style_radius(mini_cover_ph, 6, 0);
  lv_obj_clear_flag(mini_cover_ph, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(mini_cover_ph, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(mini_cover_ph, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_t *mini_ph_icon = lv_label_create(mini_cover_ph);
  lv_obj_set_style_text_font(mini_ph_icon, F_BODY, 0);
  lv_obj_set_style_text_color(mini_ph_icon, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_opa(mini_ph_icon, TXT_LEAST, 0);
  lv_label_set_text(mini_ph_icon, LV_SYMBOL_AUDIO);
  lv_obj_center(mini_ph_icon);

  // Text column (grows): eyebrow over a "title — artist" row.
  lv_obj_t *txt = lv_obj_create(mini_strip);
  lv_obj_set_height(txt, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(txt, 1);
  lv_obj_set_style_bg_opa(txt, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(txt, 0, 0);
  lv_obj_set_style_pad_all(txt, 0, 0);
  lv_obj_set_style_pad_row(txt, 3, 0);
  lv_obj_clear_flag(txt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(txt, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(txt, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(txt, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t *now_lbl = lv_label_create(txt);
  lv_obj_set_style_text_font(now_lbl, F_TINY, 0);
  lv_obj_set_style_text_color(now_lbl, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_text_letter_space(now_lbl, 2, 0);
  lv_label_set_text(now_lbl, "NOW PLAYING");
  lv_obj_add_flag(now_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

  // "title — artist" on one line: title bright, then a muted em-dash + artist.
  lv_obj_t *line = lv_obj_create(txt);
  lv_obj_set_width(line, LV_PCT(100));
  lv_obj_set_height(line, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(line, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(line, 0, 0);
  lv_obj_set_style_pad_all(line, 0, 0);
  lv_obj_set_style_pad_column(line, 7, 0);
  lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(line, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(line, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  mini_title = lv_label_create(line);
  lv_label_set_long_mode(mini_title, LV_LABEL_LONG_DOT);     // 1 line, ellipsized
  lv_obj_set_style_text_font(mini_title, F_TAB, 0);
  lv_obj_set_style_text_color(mini_title, lv_color_hex(COL_TITLE), 0);
  lv_label_set_text(mini_title, "");
  lv_obj_add_flag(mini_title, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t *dash = lv_label_create(line);
  lv_obj_set_style_text_font(dash, F_TAB, 0);
  lv_obj_set_style_text_color(dash, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_opa(dash, 107, 0);
  lv_label_set_text(dash, "-");
  lv_obj_add_flag(dash, LV_OBJ_FLAG_EVENT_BUBBLE);

  mini_artist = lv_label_create(line);
  lv_label_set_long_mode(mini_artist, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(mini_artist, 1);
  lv_obj_set_style_text_font(mini_artist, F_TAB, 0);
  lv_obj_set_style_text_color(mini_artist, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_opa(mini_artist, 107, 0);            // ~0.42
  lv_label_set_text(mini_artist, "");
  lv_obj_add_flag(mini_artist, LV_OBJ_FLAG_EVENT_BUBBLE);

  // Right: a 4-bar equalizer cue (decorative, static — flicker rule).
  static const int eq4[] = {9, 16, 11, 18};
  lv_obj_t *eq = make_equalizer(mini_strip, 4, 3, 4, eq4, COL_ACCENT);
  lv_obj_add_flag(eq, LV_OBJ_FLAG_EVENT_BUBBLE);

  mini_prog_bar = nullptr;   // the redesign mini strip has no progress bar (progress_render guards on null)
}

// Mirror the card cover's current binding onto the mini strip (UI task). The mini image shares
// the card's decoded buffer via cover_dsc (no copy), so this just re-points it — called after the
// card cover (re)binds. Set src NULL first so LVGL drops any decode cached against the OLD buffer
// (cover_dsc is reused in place, so the pointer alone wouldn't invalidate). cur_cover_buf == the
// live buffer (null on a failed/placeholder cover) -> show the placeholder square instead.
static void mini_cover_refresh() {
  if (!mini_cover_img) return;
  lv_image_set_src(mini_cover_img, NULL);
  if (cur_cover_buf) {
    lv_image_set_src(mini_cover_img, &cover_dsc);
    lv_obj_remove_flag(mini_cover_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mini_cover_ph, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(mini_cover_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(mini_cover_ph, LV_OBJ_FLAG_HIDDEN);
  }
}

static void recheck_btn_cb(lv_event_t *e);   // shared RE-CHECK tap handler, defined below

// The idle/Listening screen (card_listening): three concentric rings around a 4-bar equalizer,
// then "Listening" + a subtitle. Shown under the NOW tab when nothing is playing (apply_view).
// The whole card is the RE-CHECK affordance (a tap re-ingests) — the design shows no pill here.
static void build_listening_card() {
  card_listening = make_card(COL_BG);
  lv_obj_set_style_pad_hor(card_listening, 40, 0);
  lv_obj_set_style_pad_top(card_listening, 16, 0);
  lv_obj_set_style_pad_bottom(card_listening, 0, 0);
  lv_obj_set_flex_flow(card_listening, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card_listening, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(card_listening, LV_OBJ_FLAG_CLICKABLE);   // tap anywhere -> re-check
  lv_obj_add_event_cb(card_listening, recheck_btn_cb, LV_EVENT_CLICKED, NULL);
  build_tab_header(card_listening, 2);

  // Centered hero zone below the header.
  lv_obj_t *zone = lv_obj_create(card_listening);
  lv_obj_set_width(zone, LV_PCT(100));
  lv_obj_set_flex_grow(zone, 1);
  lv_obj_set_style_bg_opa(zone, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(zone, 0, 0);
  lv_obj_set_style_pad_all(zone, 0, 0);
  lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(zone, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_flex_flow(zone, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(zone, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Ring + equalizer halo: a 140x140 box with 3 concentric ring circles (static; the spec's
  // pulse animation is a later flicker-checked step) and a 4-bar equalizer centered on top.
  lv_obj_t *halo = lv_obj_create(zone);
  lv_obj_set_size(halo, 140, 140);
  lv_obj_set_style_bg_opa(halo, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(halo, 0, 0);
  lv_obj_set_style_pad_all(halo, 0, 0);
  lv_obj_clear_flag(halo, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(halo, LV_OBJ_FLAG_EVENT_BUBBLE);
  static const int ring_px[] = {140, 104, 70};
  for (int r = 0; r < 3; r++) {
    lv_obj_t *ring = lv_obj_create(halo);
    lv_obj_set_size(ring, ring_px[r], ring_px[r]);
    lv_obj_center(ring);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 1, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(0xADC2DD), 0);
    lv_obj_set_style_border_opa(ring, 60 - r * 14, 0);   // outer rings fainter
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ring, LV_OBJ_FLAG_EVENT_BUBBLE);
  }
  static const int eq4[] = {12, 22, 16, 26};
  lv_obj_t *eq = make_equalizer(halo, 4, 5, 5, eq4, 0xADC2DD);
  lv_obj_center(eq);

  lv_obj_t *t = lv_label_create(zone);
  lv_obj_set_style_text_font(t, F_H1, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(COL_TITLE), 0);
  lv_obj_set_style_pad_top(t, 38, 0);
  lv_label_set_text(t, "Listening");
  lv_obj_add_flag(t, LV_OBJ_FLAG_EVENT_BUBBLE);

  lv_obj_t *sub = lv_label_create(zone);
  lv_obj_set_width(sub, 360);
  lv_obj_set_style_text_font(sub, F_BODY, 0);
  lv_obj_set_style_text_color(sub, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_opa(sub, TXT_ALBUM, 0);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(sub, 10, 0);
  lv_label_set_long_mode(sub, LV_LABEL_LONG_WRAP);
  lv_label_set_text(sub, "Play something in the room - I'll recognise it and show what's on.");
  lv_obj_add_flag(sub, LV_OBJ_FLAG_EVENT_BUBBLE);
}

// Show/hide the mini strip. Guarded so it only touches LVGL on the actual flip (re-hiding an
// already-hidden object every ~4 s poll would needlessly invalidate this flicker-prone RGB panel).
static void set_mini_strip_visible(bool vis) {
  if (!mini_strip) return;
  static int last = -1;
  if ((int)vis == last) return;
  last = vis;
  if (vis) lv_obj_remove_flag(mini_strip, LV_OBJ_FLAG_HIDDEN);
  else     lv_obj_add_flag(mini_strip, LV_OBJ_FLAG_HIDDEN);
}

// b2-13 RE-CHECK button tap: ask the UART task to send a RECHECK frame (Board 1 re-ingests) and
// start the optimistic "LISTENING…" window. Same swipe guard as the tabs/strip — a page-swipe that
// grazes the button must not also fire it.
static void recheck_btn_cb(lv_event_t *e) {
  if (g_swipe_request) return;
  g_recheck_request  = true;
  g_recheck_optim_ms = ui_millis();
  ui_logf("[recheck] button tap -> RECHECK requested\n");
}

// Floating bottom pill on card_playing: [listening dot] RE-CHECK. FLOATING so it pins to the card
// bottom without disturbing the centered cover/title/progress column above it.
static void build_recheck_button(lv_obj_t *parent) {
  recheck_btn = lv_obj_create(parent);
  lv_obj_set_size(recheck_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_add_flag(recheck_btn, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(recheck_btn, LV_ALIGN_BOTTOM_MID, 0, -26);
  lv_obj_set_style_bg_color(recheck_btn, lv_color_hex(0x12141A), 0);
  lv_obj_set_style_bg_opa(recheck_btn, 184, 0);   // ~0.72 (spec's blur panel, sans blur)
  lv_obj_set_style_border_width(recheck_btn, 1, 0);
  lv_obj_set_style_border_color(recheck_btn, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_opa(recheck_btn, 28, 0);
  lv_obj_set_style_radius(recheck_btn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(recheck_btn, 22, 0);
  lv_obj_set_style_pad_ver(recheck_btn, 11, 0);
  lv_obj_set_style_pad_column(recheck_btn, 10, 0);
  lv_obj_clear_flag(recheck_btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(recheck_btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(recheck_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);   // a swipe still pages the mosaic underneath
  lv_obj_set_ext_click_area(recheck_btn, 26);                 // taps graze ~26 px above the pill — extend the target
  lv_obj_add_event_cb(recheck_btn, recheck_btn_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_set_flex_flow(recheck_btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(recheck_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  recheck_dot = lv_obj_create(recheck_btn);
  lv_obj_set_size(recheck_dot, 8, 8);
  lv_obj_set_style_radius(recheck_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(recheck_dot, 0, 0);
  lv_obj_set_style_bg_color(recheck_dot, lv_color_hex(COL_ACCENT), 0);   // accent idle; heartbeat recolors it
  lv_obj_clear_flag(recheck_dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(recheck_dot, LV_OBJ_FLAG_EVENT_BUBBLE);                // taps fall through to the pill

  recheck_lbl = lv_label_create(recheck_btn);
  lv_obj_set_style_text_font(recheck_lbl, F_SMALL, 0);
  lv_obj_set_style_text_color(recheck_lbl, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_letter_space(recheck_lbl, 2, 0);   // ~0.16em
  lv_label_set_text(recheck_lbl, "RE-CHECK");
  lv_obj_add_flag(recheck_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
}

static void ear_banner_set_down(bool down);   // defined with build_ear_banner, below

// Per-tick (UI task): reflect the optimistic LISTENING… window and the heartbeat-derived ear state
// on the button. Guarded — repaints only when the label or dot state actually changes (no per-tick
// invalidation of this flicker-prone panel).
void recheck_ui_tick() {
  if (!recheck_btn) return;
  uint32_t now = ui_millis();
  bool listening = g_recheck_optim_ms && (now - g_recheck_optim_ms < RECHECK_OPTIM_MS);
  static int last_listening = -1;
  if ((int)listening != last_listening) {
    last_listening = listening;
    lv_label_set_text(recheck_lbl, listening ? "LISTENING..." : "RE-CHECK");   // ASCII dots: built-in montserrat has no … glyph
    lv_obj_set_style_text_color(recheck_lbl, lv_color_hex(listening ? COL_ACCENT : COL_TEXT), 0);
  }
  // 0 = unknown (never heard a heartbeat), 1 = alive (fresh), 2 = down (was heard, now stale).
  int earst = (g_ear_last_ms == 0) ? 0 : ((now - g_ear_last_ms < EAR_TIMEOUT_MS) ? 1 : 2);
  static int last_earst = -1;
  if (earst != last_earst) {
    last_earst = earst;
    uint32_t pill_c = (earst == 1) ? COL_GREEN : (earst == 2) ? 0xE0524A : 0x6B7480;  // green / red / grey
    lv_obj_set_style_bg_color(recheck_dot, lv_color_hex(pill_c), 0);
    // Header status dot: green ok / amber offline (spec); grey while unknown.
    uint32_t hdr_c = (earst == 2) ? COL_AMBER : (earst == 1) ? COL_GREEN : 0x6B7480;
    for (int i = 0; i < TAB_HDRS; i++)
      if (tab_status_dot[i]) lv_obj_set_style_bg_color(tab_status_dot[i], lv_color_hex(hdr_c), 0);
    ear_banner_set_down(earst == 2);   // top-layer "ear offline" banner tracks the same state
  }
}

// b2-13 v2 knob toast. A floating card on the TOP layer so it overlays whatever card is up
// (playing/idle/album) and survives screen swaps. lv_obj_create is non-clickable by default,
// so it never steals a touch from the RE-CHECK pill below. Built once, hidden until an event.
static void build_knob_toast() {
  knob_toast = lv_obj_create(lv_layer_top());
  lv_obj_set_size(knob_toast, 320, LV_SIZE_CONTENT);
  lv_obj_align(knob_toast, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_set_style_bg_color(knob_toast, lv_color_hex(0x14161C), 0);
  lv_obj_set_style_bg_opa(knob_toast, 220, 0);   // ~0.86
  lv_obj_set_style_border_width(knob_toast, 1, 0);
  lv_obj_set_style_border_color(knob_toast, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_opa(knob_toast, 24, 0);
  lv_obj_set_style_radius(knob_toast, 18, 0);
  lv_obj_set_style_pad_ver(knob_toast, 18, 0);
  lv_obj_set_style_pad_hor(knob_toast, 22, 0);
  lv_obj_set_style_pad_row(knob_toast, 8, 0);
  lv_obj_clear_flag(knob_toast, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(knob_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_flex_flow(knob_toast, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(knob_toast, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  knob_toast_name = lv_label_create(knob_toast);   // small eyebrow label ("BRIGHTNESS")
  lv_obj_set_style_text_font(knob_toast_name, F_EYEBROW, 0);
  lv_obj_set_style_text_color(knob_toast_name, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_opa(knob_toast_name, TXT_ELAPSED, 0);
  lv_obj_set_style_text_letter_space(knob_toast_name, 2, 0);   // ~0.18em
  lv_label_set_text(knob_toast_name, "");

  knob_toast_val = lv_label_create(knob_toast);    // big value ("look 3/7", "level 24", "press")
  lv_obj_set_style_text_font(knob_toast_val, F_H2, 0);
  lv_obj_set_style_text_color(knob_toast_val, lv_color_hex(COL_TITLE), 0);
  lv_label_set_text(knob_toast_val, "");
}

// Per-tick (UI task): show the toast on a fresh KNOB_EVENT, auto-dismiss after KNOB_TOAST_MS.
void knob_toast_tick() {
  if (!knob_toast) return;
  static uint32_t last_seq = 0;
  static uint32_t shown_at = 0;
  uint32_t seq = g_knob_seq;
  if (seq != last_seq) {
    last_seq = seq;
    uint8_t id = g_knob_id, fl = g_knob_flags; int16_t v = g_knob_val;
    static const char *NAME[3] = {"MODE", "BRIGHTNESS", "GAIN"};
    lv_label_set_text(knob_toast_name, id < 3 ? NAME[id] : "KNOB");
    if (fl & 0x01) {
      lv_label_set_text(knob_toast_val, "press");
    } else {
      char b[20];
      // MODE is a discrete look index (0-based from Board 1), not a magnitude — show "look N/total".
      // bright/gain are 0-based levels (never negative).
      if (id == 0) snprintf(b, sizeof b, "look %d/%d", (int)v + 1, MODE_LOOK_COUNT);
      else         snprintf(b, sizeof b, "level %d", (int)v);
      lv_label_set_text(knob_toast_val, b);
    }
    lv_obj_clear_flag(knob_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(knob_toast);
    shown_at = ui_millis();
  }
  if (shown_at && (ui_millis() - shown_at >= KNOB_TOAST_MS)) {
    shown_at = 0;
    lv_obj_add_flag(knob_toast, LV_OBJ_FLAG_HIDDEN);
  }
}

// b2-13 v2 "ear down" banner. A top-layer pill (overlays every card) shown only when the Board 1
// heartbeat goes stale. Built once, hidden until recheck_ui_tick flips it on earst==2. Non-clickable
// (lv_obj_create default) so it never steals a touch. Knob toasts can't co-occur with it — knobs
// ride the same UART as the heartbeat, so if the ear is down no knob events arrive.
static void build_ear_banner() {
  ear_banner = lv_obj_create(lv_layer_top());
  lv_obj_set_size(ear_banner, LV_PCT(100), 42);          // full-width strip pinned to the top edge
  lv_obj_align(ear_banner, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(ear_banner, lv_color_hex(0x3C2C10), 0);   // amber wash
  lv_obj_set_style_bg_opa(ear_banner, 240, 0);   // ~0.94
  lv_obj_set_style_border_width(ear_banner, 1, 0);
  lv_obj_set_style_border_side(ear_banner, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(ear_banner, lv_color_hex(COL_AMBER), 0);
  lv_obj_set_style_border_opa(ear_banner, 140, 0);
  lv_obj_set_style_radius(ear_banner, 0, 0);
  lv_obj_set_style_pad_all(ear_banner, 8, 0);
  lv_obj_set_style_pad_column(ear_banner, 8, 0);
  lv_obj_clear_flag(ear_banner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ear_banner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_flex_flow(ear_banner, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ear_banner, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *eicon = lv_label_create(ear_banner);
  lv_obj_set_style_text_font(eicon, F_SMALL, 0);
  lv_obj_set_style_text_color(eicon, lv_color_hex(COL_AMBER), 0);
  lv_label_set_text(eicon, LV_SYMBOL_MUTE);

  lv_obj_t *lbl = lv_label_create(ear_banner);
  lv_obj_set_style_text_font(lbl, F_SMALL, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xF3E4C4), 0);
  lv_label_set_text(lbl, "Microphone offline - can't hear the room");
}

// Reflect ear liveness on the banner. Guarded by the caller (only called on an earst change).
static void ear_banner_set_down(bool down) {
  if (!ear_banner) return;
  if (down) { lv_obj_clear_flag(ear_banner, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(ear_banner); }
  else      { lv_obj_add_flag(ear_banner, LV_OBJ_FLAG_HIDDEN); }
}

void build_ui() {
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x0B0E11), 0);

  // --- NOW PLAYING (hero) -------------------------------------------------------
  // Full-bleed blurred cover backdrop + dark veil; over it the tab header, then a row:
  // 340px cover LEFT | left-aligned eyebrow/title/artist/album/progress column RIGHT.
  card_playing = make_card(COL_BG);
  lv_obj_set_style_pad_all(card_playing, 0, 0);   // backdrop is full-bleed; inner content carries its own pads
  lv_obj_set_flex_flow(card_playing, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card_playing, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Backdrop (floating, full-screen, created FIRST so it's behind everything). Shares the card
  // cover's decoded buffer via cover_dsc; COVER-scaled to fill. Hidden until a cover binds.
  cover_bg_img = lv_image_create(card_playing);
  lv_obj_add_flag(cover_bg_img, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(cover_bg_img, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(cover_bg_img, 0, 0);
  lv_image_set_inner_align(cover_bg_img, LV_IMAGE_ALIGN_COVER);
  lv_image_set_antialias(cover_bg_img, true);   // smooth the tiny-source upscale into a blur
  lv_obj_add_flag(cover_bg_img, LV_OBJ_FLAG_HIDDEN);

  // Dark veil over the backdrop (spec gradient .55->.82; ponytail: flat veil for v1, the
  // top->bottom gradient is later polish). Non-clickable so cover/pill taps pass through.
  lv_obj_t *veil = lv_obj_create(card_playing);
  lv_obj_add_flag(veil, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(veil, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(veil, 0, 0);
  lv_obj_set_style_bg_color(veil, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(veil, 200, 0);   // ~0.78
  lv_obj_set_style_border_width(veil, 0, 0);
  lv_obj_set_style_radius(veil, 0, 0);
  lv_obj_clear_flag(veil, LV_OBJ_FLAG_SCROLLABLE);

  // Inner content wrapper (above the veil): the header + cover/meta row live here, carrying the
  // 40px horizontal inset (card pad is 0 for the full-bleed backdrop).
  lv_obj_t *play_inner = lv_obj_create(card_playing);
  lv_obj_set_size(play_inner, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(play_inner, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(play_inner, 0, 0);
  lv_obj_set_style_pad_all(play_inner, 0, 0);
  lv_obj_set_style_pad_hor(play_inner, 40, 0);
  lv_obj_set_style_pad_top(play_inner, 16, 0);
  lv_obj_clear_flag(play_inner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(play_inner, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(play_inner, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  build_tab_header(play_inner, 0);     // b2-09 NOW PLAYING | RECENTLY tabs, pinned at the top

  // Cover + metadata row, centered in the remaining height.
  lv_obj_t *play_content = lv_obj_create(play_inner);
  lv_obj_set_width(play_content, LV_PCT(100));
  lv_obj_set_flex_grow(play_content, 1);
  lv_obj_set_style_bg_opa(play_content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(play_content, 0, 0);
  lv_obj_set_style_pad_all(play_content, 0, 0);
  lv_obj_set_style_pad_column(play_content, 40, 0);   // gap between cover and metadata
  lv_obj_clear_flag(play_content, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(play_content, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(play_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Cover slot (b2-04), left, 340x340. Image hidden until a cover decodes; placeholder shows
  // meanwhile. Exactly one of {cover_img, cover_ph} is visible.
  cover_img = lv_image_create(play_content);
  lv_obj_set_size(cover_img, 340, 340);
  lv_image_set_inner_align(cover_img, LV_IMAGE_ALIGN_COVER);   // fill the 340 box at any decode size
  lv_obj_set_style_radius(cover_img, 12, 0);
  lv_obj_set_style_clip_corner(cover_img, true, 0);
  lv_obj_add_flag(cover_img, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(cover_img, LV_OBJ_FLAG_CLICKABLE);       // tap the art -> album detail (b2-07)
  lv_obj_add_event_cb(cover_img, cover_tap_cb, LV_EVENT_CLICKED, NULL);

  cover_ph = lv_obj_create(play_content);
  lv_obj_set_size(cover_ph, 340, 340);
  lv_obj_set_style_bg_color(cover_ph, lv_color_hex(COL_PH), 0);
  lv_obj_set_style_border_width(cover_ph, 0, 0);
  lv_obj_set_style_radius(cover_ph, 12, 0);
  lv_obj_clear_flag(cover_ph, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(cover_ph, LV_OBJ_FLAG_CLICKABLE);        // tappable even before art decodes
  lv_obj_add_event_cb(cover_ph, cover_tap_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *ph_icon = lv_label_create(cover_ph);
  lv_obj_set_style_text_font(ph_icon, F_H1, 0);
  lv_obj_set_style_text_color(ph_icon, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_opa(ph_icon, TXT_LEAST, 0);
  lv_label_set_text(ph_icon, LV_SYMBOL_AUDIO);
  lv_obj_center(ph_icon);

  // Metadata column (right): eyebrow / title / artist / album / progress, left-aligned.
  lv_obj_t *meta_col = lv_obj_create(play_content);
  lv_obj_set_height(meta_col, LV_PCT(100));
  lv_obj_set_flex_grow(meta_col, 1);
  lv_obj_set_style_bg_opa(meta_col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(meta_col, 0, 0);
  lv_obj_set_style_pad_all(meta_col, 0, 0);
  lv_obj_clear_flag(meta_col, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(meta_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(meta_col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  // Eyebrow: 3-bar equalizer + "NOW PLAYING".
  lv_obj_t *eyebrow = lv_obj_create(meta_col);
  lv_obj_set_size(eyebrow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(eyebrow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(eyebrow, 0, 0);
  lv_obj_set_style_pad_all(eyebrow, 0, 0);
  lv_obj_set_style_pad_column(eyebrow, 8, 0);
  lv_obj_clear_flag(eyebrow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(eyebrow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(eyebrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  static const int eq3[] = {8, 14, 10};
  make_equalizer(eyebrow, 3, 3, 3, eq3, COL_ACCENT);
  lv_obj_t *eyebrow_lbl = lv_label_create(eyebrow);
  lv_obj_set_style_text_font(eyebrow_lbl, F_EYEBROW, 0);
  lv_obj_set_style_text_color(eyebrow_lbl, lv_color_hex(COL_ACCENT), 0);
  lv_obj_set_style_text_letter_space(eyebrow_lbl, 3, 0);   // ~0.22em
  lv_label_set_text(eyebrow_lbl, "NOW PLAYING");

  lbl_title  = meta_label(meta_col, F_HERO, COL_TITLE,  LV_OPA_COVER, 10);
  lv_obj_set_style_text_line_space(lbl_title, 0, 0);   // 44/1.04 tight
  lbl_artist = meta_label(meta_col, F_H2,   COL_ACCENT, LV_OPA_COVER, 12);
  lbl_album  = meta_label(meta_col, F_BODY, COL_TEXT,   TXT_ALBUM,    5);

  // --- progress bar + elapsed/-remaining time (b2-05) ------------------------
  lv_obj_t *prog_box = lv_obj_create(meta_col);
  lv_obj_set_width(prog_box, LV_PCT(100));
  lv_obj_set_style_max_width(prog_box, 380, 0);
  lv_obj_set_height(prog_box, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(prog_box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(prog_box, 0, 0);
  lv_obj_set_style_pad_all(prog_box, 0, 0);
  lv_obj_set_style_pad_top(prog_box, 30, 0);
  lv_obj_clear_flag(prog_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(prog_box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(prog_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  prog_bar = lv_bar_create(prog_box);
  lv_obj_set_width(prog_bar, LV_PCT(100));
  lv_obj_set_height(prog_bar, 5);
  // Range == the bar's pixel width: lv_bar_set_value only changes (and only flushes) when the
  // indicator moves a whole pixel (~1.3x/s), instead of ~4x/s with a 0..1000 range — fewer
  // whole-screen flushes on this flicker-prone RGB panel. (b2-05 rationale, unchanged.)
  lv_bar_set_range(prog_bar, 0, PROG_BAR_STEPS);
  lv_bar_set_value(prog_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(prog_bar, 3, 0);
  lv_obj_set_style_radius(prog_bar, 3, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(prog_bar, lv_color_hex(0xFFFFFF), 0);                 // track
  lv_obj_set_style_bg_opa(prog_bar, 33, 0);                                       // ~0.13
  lv_obj_set_style_bg_color(prog_bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
  // ponytail: no slider knob on the fill end (spec's 13px white dot) — lv_bar has none and a
  // slider would change the set_value path. Add when the bare fill reads as unfinished.

  lv_obj_t *time_row = lv_obj_create(prog_box);
  lv_obj_set_width(time_row, LV_PCT(100));
  lv_obj_set_height(time_row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(time_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(time_row, 0, 0);
  lv_obj_set_style_pad_all(time_row, 0, 0);
  lv_obj_set_style_pad_top(time_row, 9, 0);
  lv_obj_clear_flag(time_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(time_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(time_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Fixed-width edge-aligned time labels (fixed so the per-second width change doesn't reflow
  // the column — b2-05 jitter fix). Elapsed left (.70), -remaining right (.40).
  lbl_elapsed = lv_label_create(time_row);
  lv_obj_set_width(lbl_elapsed, 84);
  lv_obj_set_style_text_font(lbl_elapsed, F_SMALL, 0);
  lv_obj_set_style_text_align(lbl_elapsed, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_color(lbl_elapsed, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_opa(lbl_elapsed, TXT_ELAPSED, 0);
  lv_label_set_text(lbl_elapsed, "0:00");

  lbl_total = lv_label_create(time_row);
  lv_obj_set_width(lbl_total, 84);
  lv_obj_set_style_text_font(lbl_total, F_SMALL, 0);
  lv_obj_set_style_text_align(lbl_total, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(lbl_total, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_opa(lbl_total, TXT_FAINT, 0);
  lv_label_set_text(lbl_total, "--:--");

  // b2-13 RE-CHECK / listening pill, floating at the bottom of the now-playing card.
  build_recheck_button(card_playing);

  // --- idle: the RECENTLY PLAYED mosaic (b2-06) ---
  // A column: header, the 3x3 cover grid (or an empty-state label), and a pagination
  // dot row. Tiles are cover-only (the contract gives recent[] as {id, cover_url}); tap
  // -> album detail (b2-07). Paging is snap (swipe / dot tap), not free scroll, to keep
  // the RGB-panel tearing (b2-11) to one flick per page.
  card_idle = make_card(COL_BG);
  lv_obj_set_style_pad_hor(card_idle, 40, 0);
  lv_obj_set_style_pad_top(card_idle, 16, 0);
  lv_obj_set_style_pad_bottom(card_idle, 78, 0);   // reserve the bottom for the floating mini strip (66px)
  lv_obj_set_flex_flow(card_idle, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card_idle, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  build_tab_header(card_idle, 1);   // b2-09 NOW PLAYING | RECENTLY tabs (replaces the static title)

  // Empty state (recent_count == 0) — hidden unless there's nothing to show.
  mosaic_empty = lv_label_create(card_idle);
  lv_obj_set_style_text_font(mosaic_empty, F_BODY, 0);
  lv_obj_set_style_text_color(mosaic_empty, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_opa(mosaic_empty, TXT_ALBUM, 0);
  lv_label_set_text(mosaic_empty, LV_SYMBOL_AUDIO "  Nothing played yet");
  lv_obj_add_flag(mosaic_empty, LV_OBJ_FLAG_HIDDEN);

  // The tile grid: flex-wrap so a row of 4 fits, space-evenly across the card width.
  mosaic_grid = lv_obj_create(card_idle);
  lv_obj_set_width(mosaic_grid, LV_PCT(100));
  lv_obj_set_flex_grow(mosaic_grid, 1);
  lv_obj_set_style_bg_opa(mosaic_grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(mosaic_grid, 0, 0);
  lv_obj_set_style_pad_all(mosaic_grid, 0, 0);
  lv_obj_set_style_pad_row(mosaic_grid, 12, 0);   // 2x150 tiles + gap must fit the grid's 317px (else rows clip, squaring the corners)
  lv_obj_clear_flag(mosaic_grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(mosaic_grid, LV_OBJ_FLAG_GESTURE_BUBBLE);   // pass swipes up to card_idle
  lv_obj_set_flex_flow(mosaic_grid, LV_FLEX_FLOW_ROW_WRAP);
  // SPACE_EVENLY spreads a FULL row of 3 across the width. Empty cells on a partial last
  // page are kept in the layout (invisible, see mosaic_apply_page) rather than hidden, so
  // the grid stays a fixed 3×3 and the few real tiles keep their columns — a partial bottom
  // row therefore stays LEFT-aligned under the row above (hiding them let flex recentre the
  // remaining tiles, which read as centered).
  lv_obj_set_flex_align(mosaic_grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  for (int slot = 0; slot < MOSAIC_PER_PAGE; slot++) {
    // Each cell is a clickable rounded square (the placeholder look) that clips its art.
    lv_obj_t *cell = lv_obj_create(mosaic_grid);
    lv_obj_set_size(cell, TILE_PX, TILE_PX);
    lv_obj_set_style_bg_color(cell, lv_color_hex(COL_PH), 0);
    lv_obj_set_style_border_width(cell, 0, 0);
    lv_obj_set_style_radius(cell, 10, 0);
    lv_obj_set_style_clip_corner(cell, true, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_GESTURE_BUBBLE);   // let swipes reach card_idle
    lv_obj_set_user_data(cell, (void *)(intptr_t)slot);
    lv_obj_add_event_cb(cell, tile_tap_cb, LV_EVENT_CLICKED, (void *)(intptr_t)slot);

    lv_obj_t *g = lv_label_create(cell);                 // ♪ placeholder glyph
    lv_obj_set_style_text_font(g, F_H1, 0);
    lv_obj_set_style_text_color(g, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_opa(g, TXT_LEAST, 0);
    lv_label_set_text(g, LV_SYMBOL_AUDIO);
    lv_obj_center(g);
    lv_obj_add_flag(g, LV_OBJ_FLAG_EVENT_BUBBLE);        // taps fall through to the cell
    lv_obj_add_flag(g, LV_OBJ_FLAG_GESTURE_BUBBLE);      // ...and swipes up to card_idle

    tile_img[slot] = lv_image_create(cell);              // decoded art (hidden until bound)
    lv_obj_set_size(tile_img[slot], TILE_PX, TILE_PX);
    lv_image_set_inner_align(tile_img[slot], LV_IMAGE_ALIGN_COVER);  // fill the cell, keep aspect
    lv_obj_center(tile_img[slot]);
    lv_obj_add_flag(tile_img[slot], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tile_img[slot], LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(tile_img[slot], LV_OBJ_FLAG_GESTURE_BUBBLE);   // swipe on art -> card_idle
    tile_cell[slot] = cell;
  }

  // Pagination dots (one per page, current highlighted) — the old np_pager affordance.
  mosaic_dots = lv_obj_create(card_idle);
  lv_obj_set_width(mosaic_dots, LV_SIZE_CONTENT);
  lv_obj_set_height(mosaic_dots, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(mosaic_dots, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(mosaic_dots, 0, 0);
  lv_obj_set_style_pad_all(mosaic_dots, 0, 0);
  lv_obj_set_style_pad_top(mosaic_dots, 18, 0);
  lv_obj_set_style_pad_column(mosaic_dots, 7, 0);
  lv_obj_clear_flag(mosaic_dots, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(mosaic_dots, LV_OBJ_FLAG_GESTURE_BUBBLE);   // pass swipes up to card_idle
  lv_obj_set_flex_flow(mosaic_dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(mosaic_dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int p = 0; p < MOSAIC_MAX_PAGES; p++) {
    dot[p] = lv_obj_create(mosaic_dots);
    lv_obj_set_size(dot[p], 5, 5);                            // inactive 5x5; active grows to a pill (mosaic_apply_page)
    lv_obj_set_style_radius(dot[p], 3, 0);
    lv_obj_set_style_border_width(dot[p], 0, 0);
    lv_obj_set_style_bg_color(dot[p], lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(dot[p], 72, 0);                   // ~0.28 inactive
    lv_obj_clear_flag(dot[p], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dot[p], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(dot[p], LV_OBJ_FLAG_GESTURE_BUBBLE);
    // The dots are small but paging by tapping them must be reliable, so extend each one's
    // clickable area well beyond the visible circle (a ~50 px tap target) — taps are exact
    // (calibrated), so this is the dependable paging path even when a swipe is finicky.
    lv_obj_set_ext_click_area(dot[p], 18);
    lv_obj_add_event_cb(dot[p], dot_tap_cb, LV_EVENT_CLICKED, (void *)(intptr_t)p);
    lv_obj_add_flag(dot[p], LV_OBJ_FLAG_HIDDEN);
  }
  mosaic_apply_page();   // initial: empty until the first idle snapshot fills recent[]

  // b2-12 the compact now-playing strip, pinned to the bottom of the recently screen (floating,
  // so it doesn't disturb the mosaic above). Hidden until apply_view shows it (playing + RECENTLY).
  build_mini_strip(card_idle);

  // --- idle/Listening screen (NOW tab when nothing plays) ---
  build_listening_card();

  // --- setup placeholder (real onboarding is b2-10) ---
  card_setup = make_card(COL_BG);
  lv_obj_t *setup_lbl = lv_label_create(card_setup);
  lv_obj_set_style_text_font(setup_lbl, F_H2, 0);
  lv_obj_set_style_text_color(setup_lbl, lv_color_hex(COL_AMBER), 0);
  lv_label_set_text(setup_lbl, LV_SYMBOL_SETTINGS "  Setup");
  lv_obj_center(setup_lbl);

  // --- album detail (b2-07): two columns. LEFT 300px = back-link + cover + title/artist/meta;
  // RIGHT = scrollable tracklist (or "Tracklist unavailable" / "Loading…"). Navigation-driven
  // (a tap opens it), not state-driven — see g_album_open.
  card_album = make_card(COL_BG);
  lv_obj_set_style_pad_all(card_album, 0, 0);
  lv_obj_set_flex_flow(card_album, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card_album, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  // Left rail (300px): faint wash + 1px right border.
  lv_obj_t *album_left = lv_obj_create(card_album);
  lv_obj_set_size(album_left, 300, LV_PCT(100));
  lv_obj_set_style_bg_color(album_left, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(album_left, 4, 0);                  // ~0.015
  lv_obj_set_style_border_width(album_left, 1, 0);
  lv_obj_set_style_border_side(album_left, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_color(album_left, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_opa(album_left, 16, 0);
  lv_obj_set_style_radius(album_left, 0, 0);
  lv_obj_set_style_pad_all(album_left, 26, 0);
  lv_obj_clear_flag(album_left, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(album_left, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(album_left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  // Back-link: chevron + "RECENTLY PLAYED" eyebrow (the whole row taps back).
  lv_obj_t *back_btn = lv_obj_create(album_left);
  lv_obj_set_size(back_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(back_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(back_btn, 0, 0);
  lv_obj_set_style_pad_all(back_btn, 0, 0);
  lv_obj_set_style_pad_column(back_btn, 6, 0);
  lv_obj_clear_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(back_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_ext_click_area(back_btn, 16);
  lv_obj_add_event_cb(back_btn, back_tap_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_set_flex_flow(back_btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(back_btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *back_lbl = lv_label_create(back_btn);
  lv_obj_set_style_text_font(back_lbl, F_EYEBROW, 0);
  lv_obj_set_style_text_color(back_lbl, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_opa(back_lbl, TXT_ALBUM, 0);
  lv_obj_set_style_text_letter_space(back_lbl, 2, 0);
  lv_label_set_text(back_lbl, LV_SYMBOL_LEFT "  RECENTLY PLAYED");
  lv_obj_add_flag(back_lbl, LV_OBJ_FLAG_EVENT_BUBBLE);

  // Album cover 248x248 (decoded art over a placeholder square — exactly one visible).
  album_cover_img = lv_image_create(album_left);
  lv_obj_set_size(album_cover_img, 248, 248);
  lv_image_set_inner_align(album_cover_img, LV_IMAGE_ALIGN_COVER);
  lv_obj_set_style_radius(album_cover_img, 12, 0);
  lv_obj_set_style_clip_corner(album_cover_img, true, 0);
  lv_obj_set_style_margin_top(album_cover_img, 22, 0);
  lv_obj_add_flag(album_cover_img, LV_OBJ_FLAG_HIDDEN);

  album_cover_ph = lv_obj_create(album_left);
  lv_obj_set_size(album_cover_ph, 248, 248);
  lv_obj_set_style_bg_color(album_cover_ph, lv_color_hex(COL_PH), 0);
  lv_obj_set_style_border_width(album_cover_ph, 0, 0);
  lv_obj_set_style_radius(album_cover_ph, 12, 0);
  lv_obj_set_style_margin_top(album_cover_ph, 22, 0);
  lv_obj_clear_flag(album_cover_ph, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *album_ph_icon = lv_label_create(album_cover_ph);
  lv_obj_set_style_text_font(album_ph_icon, F_H1, 0);
  lv_obj_set_style_text_color(album_ph_icon, lv_color_hex(COL_TEXT), 0);
  lv_obj_set_style_text_opa(album_ph_icon, TXT_LEAST, 0);
  lv_label_set_text(album_ph_icon, LV_SYMBOL_AUDIO);
  lv_obj_center(album_ph_icon);

  lbl_album_title  = meta_label(album_left, F_H2,   COL_TITLE,  LV_OPA_COVER, 18);
  lbl_album_artist = meta_label(album_left, F_BODY, COL_ACCENT, LV_OPA_COVER, 6);
  lbl_album_meta   = meta_label(album_left, F_SMALL, COL_TEXT,  TXT_FAINT,    8);
  lv_obj_set_style_text_letter_space(lbl_album_meta, 1, 0);

  // Right column: the tracklist (scroll-y) + loading / unavailable states.
  lv_obj_t *album_right = lv_obj_create(card_album);
  lv_obj_set_height(album_right, LV_PCT(100));
  lv_obj_set_flex_grow(album_right, 1);
  lv_obj_set_style_bg_opa(album_right, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(album_right, 0, 0);
  lv_obj_set_style_pad_ver(album_right, 18, 0);
  lv_obj_set_style_pad_hor(album_right, 22, 0);
  lv_obj_clear_flag(album_right, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(album_right, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(album_right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  // "Loading…" / "Tracklist unavailable" — centered states. Exactly one of {loading, unavail, list}.
  lbl_album_loading = meta_label(album_right, F_BODY, COL_TEXT, TXT_ALBUM, 24);
  lv_label_set_text(lbl_album_loading, LV_SYMBOL_REFRESH "  Loading...");
  lv_obj_add_flag(lbl_album_loading, LV_OBJ_FLAG_HIDDEN);

  lbl_album_unavail = meta_label(album_right, F_BODY, COL_TEXT, TXT_ALBUM, 24);
  lv_label_set_text(lbl_album_unavail, "Tracklist unavailable");
  lv_obj_add_flag(lbl_album_unavail, LV_OBJ_FLAG_HIDDEN);

  // Scrollable tracklist (grows to fill). Free scroll TEARS on this single-framebuffer RGB panel
  // (b2-11) — accepted for this slice; the bounce-buffer fix is platform work. Rows built per open.
  album_list = lv_obj_create(album_right);
  lv_obj_set_width(album_list, LV_PCT(100));
  lv_obj_set_flex_grow(album_list, 1);
  lv_obj_set_style_bg_opa(album_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(album_list, 0, 0);
  lv_obj_set_style_pad_all(album_list, 0, 0);
  lv_obj_set_style_pad_row(album_list, 4, 0);
  lv_obj_set_flex_flow(album_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(album_list, LV_OBJ_FLAG_HIDDEN);

  // b2-13 v2: the knob toast + ear-down banner live on the top layer (overlay every card), built last.
  build_knob_toast();
  build_ear_banner();
}

// Show exactly one card state; hide the others.
void show_card(lv_obj_t *which) {
  lv_obj_t *cards[] = { card_playing, card_idle, card_setup, card_album, card_listening };
  for (lv_obj_t *c : cards) {
    if (c == which) lv_obj_remove_flag(c, LV_OBJ_FLAG_HIDDEN);
    else            lv_obj_add_flag(c, LV_OBJ_FLAG_HIDDEN);
  }
}

// b2-09 two-tab header. A tap sets g_user_view and re-applies the view off the last snapshot
// (ui_np) so the switch is instant — no wait for the next ~4 s poll. The swipe guard mirrors the
// mosaic tiles': a page-swipe that grazes the header must not also fire a tab tap.
static void tab_now_cb(lv_event_t *e) {
  if (g_swipe_request) return;
  g_user_view = VIEW_NOW;
  apply_view(ui_np);
  ui_logf("[tab] NOW PLAYING\n");
}
static void tab_recent_cb(lv_event_t *e) {
  if (g_swipe_request) return;
  g_user_view = VIEW_RECENT;
  apply_view(ui_np);
  ui_logf("[tab] RECENTLY\n");
}

// One tab cell: a clickable half-width box with a centered label and a permanent (initially
// invisible) bottom underline — style_tabs only recolors it, so toggling the highlight never
// changes geometry (no layout shift / extra flicker on this RGB panel). Inactive look by default.
static void make_tab(lv_obj_t *row, const char *txt, lv_event_cb_t cb,
                     lv_obj_t **cell_ref, lv_obj_t **lbl_ref) {
  lv_obj_t *cell = lv_obj_create(row);
  lv_obj_set_size(cell, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(cell, 0, 0);
  lv_obj_set_style_pad_hor(cell, 0, 0);
  lv_obj_set_style_pad_top(cell, 0, 0);
  lv_obj_set_style_pad_bottom(cell, 7, 0);
  lv_obj_set_style_border_side(cell, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(cell, 2, 0);                          // constant width...
  lv_obj_set_style_border_color(cell, lv_color_hex(COL_BG), 0);       // ...invisible (= card bg) until active
  lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(cell, LV_OBJ_FLAG_GESTURE_BUBBLE);                  // let a swipe reach the card
  lv_obj_set_ext_click_area(cell, 12);
  lv_obj_add_event_cb(cell, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *l = lv_label_create(cell);
  lv_obj_set_style_text_font(l, F_TAB, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0x5E6066), 0);          // inactive grey by default
  lv_obj_set_style_text_letter_space(l, 2, 0);                        // ~0.13em
  lv_label_set_text(l, txt);
  *cell_ref = cell; *lbl_ref = l;
}

// Header: left tab group ("NOW PLAYING" / "RECENTLY PLAYED", 34px gap), grow spacer, right
// status dot (mic/ear liveness). Both tabs are always present — the NOW tab shows the playing
// card or the Listening screen, the RECENTLY tab the mosaic. style_tabs sets the active highlight.
static void build_tab_header(lv_obj_t *parent, int idx) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, 46);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_style_pad_bottom(row, 14, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  lv_obj_t *tabs = lv_obj_create(row);
  lv_obj_set_size(tabs, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(tabs, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tabs, 0, 0);
  lv_obj_set_style_pad_all(tabs, 0, 0);
  lv_obj_set_style_pad_column(tabs, 34, 0);
  lv_obj_clear_flag(tabs, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(tabs, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tabs, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  make_tab(tabs, "NOW PLAYING",     tab_now_cb,    &tab_now_cell[idx], &tab_now_lbl[idx]);
  make_tab(tabs, "RECENTLY PLAYED", tab_recent_cb, &tab_rec_cell[idx], &tab_rec_lbl[idx]);

  lv_obj_t *spacer = lv_obj_create(row);
  lv_obj_set_height(spacer, 1);
  lv_obj_set_flex_grow(spacer, 1);
  lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(spacer, 0, 0);
  lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(spacer, LV_OBJ_FLAG_GESTURE_BUBBLE);

  // Right: mic/ear liveness dot (green ok / amber offline) — recheck_ui_tick recolors it.
  // ponytail: clock omitted — no time source on the device yet (would show a wrong time).
  tab_status_dot[idx] = lv_obj_create(row);
  lv_obj_set_size(tab_status_dot[idx], 7, 7);
  lv_obj_set_style_radius(tab_status_dot[idx], LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(tab_status_dot[idx], 0, 0);
  lv_obj_set_style_bg_color(tab_status_dot[idx], lv_color_hex(COL_GREEN), 0);
  lv_obj_set_style_margin_bottom(tab_status_dot[idx], 6, 0);
  lv_obj_clear_flag(tab_status_dot[idx], LV_OBJ_FLAG_SCROLLABLE);
}

// Highlight the tab matching the shown card on ALL header copies. Guarded so it only repaints
// when the active view actually flips (every redraw flickers this RGB panel). NOW = card_playing
// OR card_listening; RECENTLY = card_idle (mosaic); setup/album have no header, leave tabs as-is.
static void style_tabs(lv_obj_t *active_card) {
  int active = (active_card == card_playing || active_card == card_listening) ? VIEW_NOW
             : (active_card == card_idle)                                     ? VIEW_RECENT : -1;
  if (active < 0) return;
  static int last_active = -2;
  if (active == last_active) return;
  last_active = active;
  for (int i = 0; i < TAB_HDRS; i++) {
    bool now_on = (active == VIEW_NOW), rec_on = (active == VIEW_RECENT);
    uint32_t on_txt = COL_ACCENT, off_txt = 0x5E6066, on_bar = COL_ACCENT, off_bar = COL_BG;
    if (tab_now_lbl[i])  lv_obj_set_style_text_color(tab_now_lbl[i], lv_color_hex(now_on ? on_txt : off_txt), 0);
    if (tab_rec_lbl[i])  lv_obj_set_style_text_color(tab_rec_lbl[i], lv_color_hex(rec_on ? on_txt : off_txt), 0);
    if (tab_now_cell[i]) lv_obj_set_style_border_color(tab_now_cell[i], lv_color_hex(now_on ? on_bar : off_bar), 0);
    if (tab_rec_cell[i]) lv_obj_set_style_border_color(tab_rec_cell[i], lv_color_hex(rec_on ? on_bar : off_bar), 0);
  }
}

// b2-09 pick the active card from state + the user's tab choice, populate/free the mosaic to
// match, toggle the NOW PLAYING tab (only meaningful while playing), highlight the active tab,
// and show it (unless album detail is up — it overrides). Called every poll (render_now_playing)
// AND on a tab tap (off ui_np), so a view switch is instant and the choice survives the ~4 s poll.
// Keeps ui_np fresh so a between-poll tab tap has the latest recent[].
static void apply_view(const NowPlaying &np) {
  ui_np = np;
  bool playing = np_is_playing(np);
  // Both tabs always present. RECENTLY -> the mosaic (playing or idle). NOW -> the playing card
  // when something plays, else the Listening screen. setup state overrides.
  lv_obj_t *want;
  if (strcmp(np.state, "setup") == 0)  want = card_setup;
  else if (g_user_view == VIEW_RECENT) want = card_idle;        // mosaic
  else if (playing)                    want = card_playing;     // NOW + playing
  else                                 want = card_listening;   // NOW + idle -> Listening

  g_active_card = want;
  if (want == card_idle) { g_show_mosaic = true;  mosaic_on_snapshot(np); }
  else                   { g_show_mosaic = false; mosaic_clear_images();  }
  style_tabs(want);
  // b2-12: the mini now-playing strip rides the mosaic only while a track is playing (Pi's
  // shows_now_strip = playing && VIEW_RECENT).
  set_mini_strip_visible(want == card_idle && playing);
  if (!g_album_open) show_card(want);
}

// Open the album detail for `id` (UI task). Show the card in its loading state immediately
// (so the tap feels instant), then hand the id to the net task to fetch GET /album/{id};
// ui_apply_album fills the tracklist when it returns. g_album_open gates the poll's show_card
// so the next snapshot won't clobber this screen.
static void open_album(const char *id) {
  if (!id || id[0] == '\0') return;
  g_album_open = true;
  album_show_loading();
  show_card(card_album);
  ui_request_album(id);
  ui_logf("[album] open id=%s (loading)\n", id);
}

// Leave album detail (UI task): clear the flag, reclaim the header art, and restore the card
// the live poll wants (g_active_card; falls back to idle before the first snapshot).
void close_album(void) {
  if (!g_album_open) return;
  g_album_open = false;
  if (album_cover_img) lv_image_set_src(album_cover_img, NULL);
  if (cur_album_cover_buf) { ui_free(cur_album_cover_buf); cur_album_cover_buf = nullptr; }
  show_card(g_active_card ? g_active_card : card_idle);
  ui_logf("[album] back -> prior card\n");
}

// "S:SS" / "M:SS" elapsed-time formatting (no hours — tracks are minutes long).
static void fmt_mmss(char *out, size_t n, float secs) {
  if (secs < 0) secs = 0;
  int s = (int)(secs + 0.5f);
  snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

// Per-tick bar update (UI task). Interpolates the displayed position LOCALLY from the
// last poll's anchor — serverPos + (now - recvMillis) — clamps to [0, duration], and
// re-renders the bar + time labels. `force` bypasses the redraw throttle (used right
// after a poll so a track change / re-snap shows immediately). With unknown duration we
// can't form a fraction, so we hide the bar (never a misleading empty/full track) and
// just advance the elapsed text. Cheap and idempotent; safe to call every loop.
// Set a label's text only if it actually changed — avoids re-laying-out / repainting the
// label 4x/second when the displayed second (or the static total) hasn't moved.
static void label_set_if_changed(lv_obj_t *lbl, char *cache, size_t n, const char *txt) {
  if (strncmp(cache, txt, n) == 0) return;
  strncpy(cache, txt, n - 1);
  cache[n - 1] = '\0';
  lv_label_set_text(lbl, txt);
}

void progress_render(bool force) {
  if (!prog_playing || !prog_bar) return;

  static uint32_t last_draw_ms = 0, last_log_ms = 0;
  uint32_t now = ui_millis();
  // 1 Hz free-running cadence: the elapsed time is second-resolution and the bar only
  // advances ~1.4 px/s, so updating faster just multiplies whole-screen flicker on this
  // RGB panel (every framebuffer write glitches the scanout) for no visible benefit. Polls
  // still re-snap immediately via the `force` path, so track changes stay instant.
  if (!force && now - last_draw_ms < 1000) return;
  last_draw_ms = now;

  // Cache the last applied widget state so we only touch LVGL when something changes.
  static char last_elapsed[8] = {0}, last_total[8] = {0};
  static int  last_val = -1;
  static int  last_hidden = -1;   // -1 unknown, 0 shown, 1 hidden

  float elapsed = (int32_t)(now - prog_recv_ms) / 1000.0f;   // local term since last poll
  float pos = prog_server_pos + elapsed;
  if (pos < 0) pos = 0;

  char a[8];
  if (prog_has_dur && prog_duration > 0.0f) {
    if (pos > prog_duration) pos = prog_duration;            // clamp [0, duration]
    int val = (int)((pos / prog_duration) * PROG_BAR_STEPS + 0.5f);
    if (val != last_val) {
      lv_bar_set_value(prog_bar, val, LV_ANIM_OFF);          // ANIM_OFF: never tween (track change snaps)
      if (mini_prog_bar) lv_bar_set_value(mini_prog_bar, val, LV_ANIM_OFF);  // b2-12 mirror onto the strip
      last_val = val;
    }
    if (last_hidden != 0) {
      lv_obj_remove_flag(prog_bar, LV_OBJ_FLAG_HIDDEN);
      if (mini_prog_bar) lv_obj_remove_flag(mini_prog_bar, LV_OBJ_FLAG_HIDDEN);
      last_hidden = 0;
    }
    char b[10];
    fmt_mmss(a, sizeof(a), pos);
    float rem = prog_duration - pos; if (rem < 0) rem = 0;   // spec shows -remaining, not total
    b[0] = '-'; fmt_mmss(b + 1, sizeof(b) - 1, rem);
    label_set_if_changed(lbl_elapsed, last_elapsed, sizeof(last_elapsed), a);
    label_set_if_changed(lbl_total,   last_total,   sizeof(last_total),   b);
  } else {                                                   // unknown duration -> indeterminate
    if (last_hidden != 1) {
      lv_obj_add_flag(prog_bar, LV_OBJ_FLAG_HIDDEN);
      if (mini_prog_bar) lv_obj_add_flag(mini_prog_bar, LV_OBJ_FLAG_HIDDEN);
      last_hidden = 1; last_val = -1;
    }
    fmt_mmss(a, sizeof(a), pos);
    label_set_if_changed(lbl_elapsed, last_elapsed, sizeof(last_elapsed), a);
    label_set_if_changed(lbl_total,   last_total,   sizeof(last_total),   "--:--");
  }

  if (force || now - last_log_ms > 2000) {                   // serial evidence of advance + re-snap
    last_log_ms = now;
    ui_logf("[bar] interp=%.1fs (server=%.1f +%.1f) dur=%.1f\n",
                  pos, prog_server_pos, elapsed, prog_has_dur ? prog_duration : -1.0f);
  }
}

// Re-anchor the interpolation from a fresh poll (UI task). Overwrites serverPos/recvMillis
// wholesale — the only free-running term is local elapsed-since-this-poll, reset every
// poll. A track_id change snaps cleanly (ANIM_OFF + the new anchor; no tween from old pos).
static void progress_on_snapshot(const NowPlaying &np) {
  if (!np_is_playing(np)) { prog_playing = false; return; }

  bool track_changed = strncmp(prog_track_id, np.track_id, sizeof(prog_track_id)) != 0;
  if (track_changed) {
    strncpy(prog_track_id, np.track_id, sizeof(prog_track_id) - 1);
    prog_track_id[sizeof(prog_track_id) - 1] = '\0';
  }
  prog_playing    = true;
  prog_has_dur    = np.has_duration;
  prog_duration   = np.duration_s;
  prog_server_pos = np.has_position ? np.position_s : 0.0f;
  prog_recv_ms    = np.received_ms;
  progress_render(true);   // immediate re-snap (and immediate jump on a track change)
}

// Render a snapshot onto the card. Defensive: any track field may be empty even
// while "playing" (metadata-worker lag) — show what's present, never crash.
void render_now_playing(const NowPlaying &np) {
  if (strcmp(np.state, "playing") == 0) {
    // Only re-set a label when its text changed. render runs every poll (~4 s) but the
    // track text is usually identical between polls; re-setting it would invalidate and
    // re-flush the whole text block each time — a large redraw that flickers this RGB
    // panel (every framebuffer write does). Guarding keeps the screen static between
    // genuine track changes. Caches are sized to the label buffers. Labels stay current even
    // when VIEW_RECENT hides card_playing, so a tab-back to NOW is instant and fresh.
    static char c_title[128] = {0}, c_artist[128] = {0}, c_album[128] = {0};
    label_set_if_changed(lbl_title,  c_title,  sizeof(c_title),  np.title);
    label_set_if_changed(lbl_artist, c_artist, sizeof(c_artist), np.artist);
    label_set_if_changed(lbl_album,  c_album,  sizeof(c_album),  np.album);
    // b2-12 mirror title/artist onto the mini strip (separate caches; it stays current even while
    // hidden on the NOW view, so a flip to RECENTLY mid-song is instant).
    static char c_mtitle[128] = {0}, c_martist[128] = {0};
    if (mini_title)  label_set_if_changed(mini_title,  c_mtitle,  sizeof(c_mtitle),  np.title);
    if (mini_artist) label_set_if_changed(mini_artist, c_martist, sizeof(c_martist), np.artist);
    cpystr(ui_track_id, np.track_id, sizeof(ui_track_id));  // the cover tap opens THIS album
  } else {
    ui_track_id[0] = '\0';   // setup / idle: no current track to open
  }
  // b2-09: the active card is state + the user's tab choice. apply_view does the mosaic
  // populate/free, the tab highlight, g_active_card, and the show (gated by g_album_open so the
  // album detail still survives the poll). The view choice persists across polls, so a user who
  // tabbed to RECENTLY while playing isn't yanked back to NOW every ~4 s.
  apply_view(np);
  progress_on_snapshot(np);   // re-anchor (or stop) the bar interpolation
}

// (moved) Bind a decoded cover to an image/placeholder pair, freeing the prior buffer.
static void bind_cover_impl(lv_obj_t *img, lv_obj_t *ph, lv_image_dsc_t *dsc,
                            uint16_t **cur, const CoverMsg &m, const char *tag) {
  lv_image_set_src(img, NULL);
  if (*cur) { ui_free(*cur); *cur = nullptr; }

  if (m.ok && m.buf) {
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic  = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf     = LV_COLOR_FORMAT_RGB565;
    dsc->header.w      = m.w;
    dsc->header.h      = m.h;
    dsc->header.stride = (uint32_t)m.w * 2;
    dsc->data          = (const uint8_t *)m.buf;
    dsc->data_size     = (size_t)m.w * m.h * 2;
    lv_image_set_src(img, dsc);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ph, LV_OBJ_FLAG_HIDDEN);
    *cur = m.buf;
    ui_logf("[ui] %s bound %ux%u (free PSRAM %u)\n",
                  tag, m.w, m.h, (unsigned)ui_free_psram());
  } else {
    lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ph, LV_OBJ_FLAG_HIDDEN);
    ui_logf("[ui] %s -> placeholder\n", tag);
  }
}

// Box-average the decoded cover down into bg_small[] and bind it to the full-bleed backdrop.
// LVGL's anti-aliased scaler upscales the tiny source to fill the screen -> a soft blur.
static void make_backdrop(const CoverMsg &m) {
  if (!cover_bg_img) return;
  if (!(m.ok && m.buf && m.w && m.h)) { lv_obj_add_flag(cover_bg_img, LV_OBJ_FLAG_HIDDEN); return; }
  const uint16_t *src = m.buf;
  for (int ty = 0; ty < BG_H; ty++) {
    int y0 = ty * m.h / BG_H, y1 = (ty + 1) * m.h / BG_H; if (y1 <= y0) y1 = y0 + 1;
    for (int tx = 0; tx < BG_W; tx++) {
      int x0 = tx * m.w / BG_W, x1 = (tx + 1) * m.w / BG_W; if (x1 <= x0) x1 = x0 + 1;
      uint32_t r = 0, g = 0, b = 0, n = 0;
      for (int y = y0; y < y1; y++) {
        const uint16_t *row = src + (size_t)y * m.w;
        for (int x = x0; x < x1; x++) {
          uint16_t p = row[x];
          r += (p >> 11) & 0x1F; g += (p >> 5) & 0x3F; b += p & 0x1F; n++;
        }
      }
      if (!n) n = 1;
      bg_small[ty * BG_W + tx] = (uint16_t)(((r / n) << 11) | ((g / n) << 5) | (b / n));
    }
  }
  memset(&bg_dsc, 0, sizeof(bg_dsc));
  bg_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
  bg_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
  bg_dsc.header.w      = BG_W;
  bg_dsc.header.h      = BG_H;
  bg_dsc.header.stride = BG_W * 2;
  bg_dsc.data          = (const uint8_t *)bg_small;
  bg_dsc.data_size     = sizeof(bg_small);
  lv_image_set_src(cover_bg_img, NULL);
  lv_image_set_src(cover_bg_img, &bg_dsc);
  lv_obj_remove_flag(cover_bg_img, LV_OBJ_FLAG_HIDDEN);
}

void bind_cover(const CoverMsg &m) {
  bind_cover_impl(cover_img, cover_ph, &cover_dsc, &cur_cover_buf, m, "cover");
  mini_cover_refresh();      // b2-12 mirror onto the mini strip
  make_backdrop(m);          // soft-blur full-bleed backdrop from the same decoded art
}

void bind_album_cover(const CoverMsg &m) {
  bind_cover_impl(album_cover_img, album_cover_ph, &album_cover_dsc, &cur_album_cover_buf, m, "album-cover");
}

void ui_apply_album(const Album &a) {
  if (!g_album_open) return;
  lv_obj_add_flag(lbl_album_loading, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(lbl_album_title,  a.title);
  lv_label_set_text(lbl_album_artist, a.artist);
  // Meta: track count only — the contract gives no album duration (ponytail: add "· {dur}" if a
  // duration field is ever added to Album).
  char meta[32];
  snprintf(meta, sizeof meta, "%u TRACKS", a.track_count);
  lv_label_set_text(lbl_album_meta, a.track_count ? meta : "");
  lv_obj_clean(album_list);

  if (!a.ok || a.track_count == 0) {                       // miss path (§7 "tracklist fetch fails")
    lv_obj_add_flag(album_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lbl_album_unavail, LV_OBJ_FLAG_HIDDEN);
    ui_logf("[ui] album id=%s -> Tracklist unavailable\n", a.id);
    return;
  }
  lv_obj_add_flag(lbl_album_unavail, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *first_heard = nullptr;
  for (int i = 0; i < a.track_count; i++) {
    const AlbumTrack &t = a.tracks[i];
    // Row: [number, right-aligned faint] [title, grows] [✓ HEARD badge when heard]. 46px, radius 9.
    // ponytail: no "currently-playing" row highlight/equalizer — the contract doesn't flag which
    // track is live (AlbumTrack drops apple_id), so we can't identify it. HEARD marks come from t.heard.
    lv_obj_t *row = lv_obj_create(album_list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 46);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 9, 0);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *num = lv_label_create(row);
    lv_obj_set_width(num, 22);
    lv_obj_set_style_text_font(num, F_BODY, 0);
    lv_obj_set_style_text_align(num, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(num, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_style_text_opa(num, TXT_LEAST, 0);
    char nb[8]; snprintf(nb, sizeof nb, "%u", t.number);
    lv_label_set_text(num, nb);

    lv_obj_t *tt = lv_label_create(row);
    lv_obj_set_flex_grow(tt, 1);
    lv_label_set_long_mode(tt, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(tt, F_BODY, 0);
    lv_obj_set_style_text_color(tt, lv_color_hex(t.heard ? COL_TITLE : COL_TEXT), 0);
    lv_obj_set_style_text_opa(tt, t.heard ? LV_OPA_COVER : 209, 0);   // heard brighter, else ~0.82
    lv_label_set_text(tt, t.title);

    if (t.heard) {
      lv_obj_t *hb = lv_label_create(row);
      lv_obj_set_style_text_font(hb, F_TINY, 0);
      lv_obj_set_style_text_color(hb, lv_color_hex(COL_ACCENT), 0);
      lv_obj_set_style_text_letter_space(hb, 1, 0);
      lv_label_set_text(hb, LV_SYMBOL_OK "  HEARD");
      if (!first_heard) first_heard = row;
    }
  }
  lv_obj_remove_flag(album_list, LV_OBJ_FLAG_HIDDEN);
  if (first_heard) lv_obj_scroll_to_view(first_heard, LV_ANIM_OFF);  // bring the heard track on-screen
  ui_logf("[ui] album id=%s \"%s\" by \"%s\": %u tracks%s\n",
                a.id, a.title, a.artist, a.track_count, first_heard ? " (heard row shown)" : "");
}

void ui_apply_tile(const TileMsg &m) {
  if (m.slot >= MOSAIC_PER_PAGE) { if (m.buf) ui_free(m.buf); return; }
  lv_image_set_src(tile_img[m.slot], NULL);
  if (tile_buf[m.slot]) { ui_free(tile_buf[m.slot]); tile_buf[m.slot] = nullptr; }

  if (m.ok && m.buf) {
    lv_image_dsc_t &d = tile_dsc[m.slot];
    memset(&d, 0, sizeof(d));
    d.header.magic  = LV_IMAGE_HEADER_MAGIC;
    d.header.cf     = LV_COLOR_FORMAT_RGB565;
    d.header.w      = m.w;
    d.header.h      = m.h;
    d.header.stride = (uint32_t)m.w * 2;
    d.data          = (const uint8_t *)m.buf;
    d.data_size     = (size_t)m.w * m.h * 2;
    lv_image_set_src(tile_img[m.slot], &d);
    lv_obj_remove_flag(tile_img[m.slot], LV_OBJ_FLAG_HIDDEN);
    tile_buf[m.slot] = m.buf;
  } else {
    lv_obj_add_flag(tile_img[m.slot], LV_OBJ_FLAG_HIDDEN);   // show the placeholder square
  }
}

void cpystr(char *dst, const char *src, size_t n) {
  if (!src) src = "";
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}
