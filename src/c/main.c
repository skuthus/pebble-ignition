// Ignition — a booster engine plate as a watchface, for Pebble Round 2
// (gabbro) and Time Round (chalk).
//
// Engines (33, in the 3 / 10 / 20 layout) tell the time:
//   AM/PM  — the top center engine: lit = AM, dark = PM.
//   Hour   — the other 12 inner engines (2 lower center + 10 inner ring), 1-12 lit.
//   Minute — the outer 20, one per 3 minutes. The newest burns white-hot.
// Engine order (setting): Dial lights each group clockwise so it reads like a
// clock; Startup lights them in a startup-style sequence of opposite pairs.
// Opening the face plays the full startup sequence, then settles on the time.
// Optional: the current minute engine pulses (slow, then fast, then lit).
// Easter egg: at midnight on the day Mars is closest to Earth, the inner engines
// become a pair of maracas for an hour, with a quick double buzz.
//
// Rim: a launch-broadcast mission timeline showing one complication at a time.
// Each has its own color and node pattern:
//   Hours    — 12 white nodes, jump hour.
//   Steps    — 10 flame-colored nodes, 10% of the goal each.
//   Day      — sky blue; 24h with midnight at top, nodes at sunrise, solar noon
//              and sunset (computed on the phone from location).
//   Month    — pale yellow; one node per day of the month, jump day.
//   T-minus  — red-orange countdown to a date. Long count: 10 nodes of the wait
//              since the date was set. Final 30 days: one node per day.
//   Moon     — moonlight; one lunar cycle with new moon at top and nodes at the
//              four principal phases. The marker shows the current phase.
//   Seconds  — mint green; a seconds hand travelling the rim, 12 nodes.
//   Moon & Mars — how far each is from Earth: 12 o'clock is closest (Earth-blue
//              node), 6 o'clock farthest, scaled to each body's own range. Left
//              side = approaching, right = moving away. Distances come from the
//              phone (lunar series and JPL orbital elements).
// A wrist flick (or a tap on the Round 2's touchscreen) cycles through the
// enabled complications, or shows one chosen complication (setting); the rim
// returns to the default after a few seconds.
//
// Motion: engines flare as they ignite and flame out in sequence as they shut
// down; the rim sweeps out from 12 o'clock when it changes; the date band opens
// from the center. One frame timer drives it all and stops when nothing moves.
#include <pebble.h>

#define SETTINGS_VERSION_KEY 1
#define SETTINGS_KEY         2
#define SUN_KEY              3
#define ORBIT_KEY            4
#define SETTINGS_VERSION     2

#define DESIGN_R  130   // geometry is designed at gabbro's radius and scaled
#define RIM_R     114   // rim track; marker halos stay >= 4px inside the screen edge
#define HALO_R    9     // standard marker halo
#define BODY_HALO 11    // Moon / Mars marker halo
#define ENGINES   33
#define ANIM_MS   30
#define HOLD_MS   450  // all 33 lit at the end of the startup sequence
#define DAY_MIN   1440
#define NO_TIME   (-1)
#define NO_DATE   INT32_MIN
#define TERMINAL_DAYS 30
#define SETTINGS_REV  5    // bump when appending fields that need non-zero defaults

enum { FLAME_ORANGE = 0, FLAME_METHANE, FLAME_WHITE, FLAME_COUNT };
enum { RIM_HOURS = 0, RIM_STEPS, RIM_DAY, RIM_MONTH, RIM_TMINUS, RIM_MOON, RIM_ORBITS, RIM_SECONDS,
       RIM_COUNT };
enum { ORDER_DIAL = 0, ORDER_STARTUP = 1 };
enum { FLICK_CYCLE = 0, FLICK_ONE = 1, FLICK_ARRANGE_OLD = 2 };   // 2: pre-release mode, now a toggle
enum { READOUT_BAND = 0, READOUT_THROTTLE = 1 };

typedef struct {
  uint8_t flame;
  bool vibe_bt;
  // v1.1
  uint8_t rim;          // default complication
  bool flick_swap;
  int32_t step_goal;
  // v1.2 — older saved settings load with the defaults for these.
  bool show_steps, show_day, show_month, show_tminus;
  int32_t tminus_day;   // target, as days since 1970-01-01, or NO_DATE
  int32_t tminus_start; // day the target was set
  uint8_t engine_order;
  // v1.3 — these fit in what was padding, so older blobs load them as zero;
  // `rev` tells load_state to apply their real defaults.
  uint8_t rev;
  bool show_moon;
  bool show_orbits;     // v1.4, rev 4
  // v1.5 — beyond the end of older blobs, so these load with their defaults.
  uint8_t flick_mode;   // FLICK_CYCLE or FLICK_ONE
  uint8_t flick_target; // complication a flick shows in FLICK_ONE mode
  bool show_date;       // flick also shows the date readout
  bool tap_screen;      // a tap on the touchscreen does what a flick does
  bool pulse_minutes;   // the current minute engine pulses (battery cost)
  bool flick_rearrange; // a flick also swaps the engine order while it's up
  bool show_seconds;    // rev 5: fits in old padding, so the rev check sets it
  uint8_t flick_secs;   // how long a flick stays up: 5, 8 or 10 (0 in old blobs -> 8)
  uint8_t readout;      // what Show date shows: READOUT_BAND, or READOUT_THROTTLE (0 in old blobs)
} Settings;

typedef struct {
  int16_t rise, set, noon;   // minutes after local midnight, or NO_TIME
  int16_t yday;              // day of year they were computed for
  int16_t south;             // 1 south of the equator: the moon's lit side flips
} SunTimes;

static Window *s_window;
static Settings s_cfg;
static SunTimes s_sun = { NO_TIME, NO_TIME, NO_TIME, -1, 0 };

typedef struct {
  int32_t moon_km;           // 0 = no data yet
  int32_t mars_mau;          // thousandths of an AU
  uint8_t moon_in, mars_in;  // 1 = getting closer
  time_t updated;
  int32_t mars_closest_day;  // local day of Mars's next closest approach, 0 = unknown
} Orbits;
static Orbits s_orb;
static AppTimer *s_anim;
static AppTimer *s_swap_timer;

static struct tm s_now;
static bool s_connected = true;
static int s_showing = -1;       // complication a flick put up, or -1 for the default
static int32_t s_steps = 0;
static int s_anim_step = -1;     // startup sequence: engines lit so far, or -1 when done
static uint64_t s_startup_t0;    // when the startup sequence began
static int s_r = DESIGN_R;
static void update_tick_subscription(void);

// Flame palettes: outer rim, mid, core. All on the 64-color palette.
static const uint32_t FLAMES[FLAME_COUNT][3] = {
  { 0xFF5500, 0xFFAA00, 0xFFFFAA },   // orange
  { 0x5555FF, 0xAA55FF, 0xFFAAFF },   // methane blue-violet
  { 0xAAAAAA, 0xFFFFFF, 0xFFFFFF },   // white
};

#define COLOR_DAY    0x55AAFF
#define COLOR_MONTH  0xFFFFAA
#define COLOR_TMINUS 0xFF5555
#define COLOR_MOON   0xAAAAFF
#define COLOR_MARS   0xFF5500
#define COLOR_EARTH  0x55AAFF
#define COLOR_SECONDS 0x55FFAA

// ---------------------------------------------------------------- dates

static int32_t days_from_civil(int y, int m, int d) {   // m 1-12
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400;
  int doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static int32_t today_day(void) {
  return days_from_civil(s_now.tm_year + 1900, s_now.tm_mon + 1, s_now.tm_mday);
}

static int days_in_month(int year, int mon0) {
  static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return (mon0 == 1 && leap) ? 29 : d[mon0 % 12];
}

// "YYYY-MM-DD" -> days since 1970, or NO_DATE.
static int32_t parse_date(const char *s) {
  if (!s || strlen(s) < 10 || s[4] != '-' || s[7] != '-') return NO_DATE;
  int y = atoi(s), m = atoi(s + 5), d = atoi(s + 8);
  if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return NO_DATE;
  return days_from_civil(y, m, d);
}

// ---------------------------------------------------------------- settings

static void default_settings(void) {
  s_cfg.flame = FLAME_ORANGE;
  s_cfg.vibe_bt = true;
  s_cfg.rim = RIM_MONTH;
  s_cfg.flick_swap = true;
  s_cfg.step_goal = 8000;
  s_cfg.show_steps = true;
  s_cfg.show_day = true;
  s_cfg.show_month = true;
  s_cfg.show_tminus = false;
  s_cfg.tminus_day = NO_DATE;
  s_cfg.tminus_start = NO_DATE;
  s_cfg.engine_order = ORDER_DIAL;
  s_cfg.rev = SETTINGS_REV;
  s_cfg.show_moon = true;
  s_cfg.show_orbits = true;
  s_cfg.flick_mode = FLICK_CYCLE;
  s_cfg.flick_target = RIM_STEPS;
  s_cfg.show_date = true;
  s_cfg.tap_screen = true;
  s_cfg.pulse_minutes = false;
  s_cfg.flick_rearrange = false;
  s_cfg.show_seconds = true;
  s_cfg.flick_secs = 8;
  s_cfg.readout = READOUT_BAND;
}

static void load_state(void) {
  default_settings();
  if (persist_exists(SETTINGS_VERSION_KEY) &&
      persist_read_int(SETTINGS_VERSION_KEY) == SETTINGS_VERSION &&
      persist_exists(SETTINGS_KEY)) {
    int stored = persist_get_size(SETTINGS_KEY);
    persist_read_data(SETTINGS_KEY, &s_cfg, sizeof(s_cfg));
    bool migrated = false;
    // Saved before the engines showed the hour (v1.1 and older): the rim moves
    // off Hours, now redundant, to Month.
    if (stored <= (int)__builtin_offsetof(Settings, show_steps)) {
      if (s_cfg.rim == RIM_HOURS) s_cfg.rim = RIM_MONTH;
      s_cfg.engine_order = ORDER_DIAL;
      migrated = true;
    }
    // Saved before the Moon complication (v1.2): turn it on by default.
    if (s_cfg.rev < 3) {
      s_cfg.show_moon = true;
      migrated = true;
    }
    // Saved before the Moon & Mars complication (v1.3): turn it on by default.
    // Saved before the Seconds complication: turn it on by default.
    if (s_cfg.rev < 5) {
      s_cfg.show_seconds = true;
      migrated = true;
    }
    if (s_cfg.rev < 4) {
      s_cfg.show_orbits = true;
      migrated = true;
    }
    if (migrated) {
      s_cfg.rev = SETTINGS_REV;
      persist_write_data(SETTINGS_KEY, &s_cfg, sizeof(s_cfg));   // migrate once
    }
  }
  if (s_cfg.flame >= FLAME_COUNT) s_cfg.flame = FLAME_ORANGE;
  if (s_cfg.rim >= RIM_COUNT) s_cfg.rim = RIM_HOURS;
  if (s_cfg.step_goal <= 0) s_cfg.step_goal = 8000;
  if (s_cfg.engine_order > ORDER_STARTUP) s_cfg.engine_order = ORDER_DIAL;
  // The short-lived "Rearrange engines" flick mode became a separate toggle.
  if (s_cfg.flick_mode == FLICK_ARRANGE_OLD) {
    s_cfg.flick_mode = FLICK_CYCLE;
    s_cfg.flick_rearrange = true;
  }
  if (s_cfg.flick_mode > FLICK_ONE) s_cfg.flick_mode = FLICK_CYCLE;
  if (s_cfg.flick_secs != 5 && s_cfg.flick_secs != 8 && s_cfg.flick_secs != 10) s_cfg.flick_secs = 8;
  if (s_cfg.flick_target >= RIM_COUNT) s_cfg.flick_target = RIM_STEPS;
  if (s_cfg.readout > READOUT_THROTTLE) s_cfg.readout = READOUT_BAND;
  if (persist_exists(SUN_KEY)) persist_read_data(SUN_KEY, &s_sun, sizeof(s_sun));
  if (persist_exists(ORBIT_KEY)) persist_read_data(ORBIT_KEY, &s_orb, sizeof(s_orb));
}

static void save_settings(void) {
  persist_write_int(SETTINGS_VERSION_KEY, SETTINGS_VERSION);
  persist_write_data(SETTINGS_KEY, &s_cfg, sizeof(s_cfg));
}

// ---------------------------------------------------------------- engine layout

static int sc(int v) { return v * s_r / DESIGN_R; }

// ---------------------------------------------------------------- motion
//
// Short transitions run on channels. A single frame timer redraws while any
// channel is active and stops when they have all finished.

#define FX_FRAME_MS     33
#define RIM_SWEEP_MS    550
#define BAND_OPEN_MS    220
#define BAND_CLOSE_MS   180
#define SHAKE_MS        1200
#define FLARE_MS        420   // one engine igniting
#define FLAMEOUT_MS     120   // core dies just before an engine goes dark
#define OFF_SPAN_MS     420   // a whole shutdown sequence, first to last engine
#define ON_STEP_MS      70    // stagger between engines igniting together

enum { FX_RIM, FX_ENGINES, FX_BAND, FX_SHAKE, FX_STARTUP, FX_THROTTLE, FX_COUNT };

static void td_phase_end(void);

typedef struct {
  uint64_t start;
  uint16_t dur;
  bool on;
} Fx;

static Fx s_fx[FX_COUNT];
static AppTimer *s_fx_timer;
static bool s_band_closing = false;
static Layer *s_canvas;
static bool s_date_up;

// Milliseconds for animation timing, kept moving forward. If the wall clock is
// set back (a time sync from the phone, by milliseconds or by hours), the jump
// is absorbed into an offset so running animations carry on instead of
// stalling until the clock catches up.
static uint64_t now_ms(void) {
  static uint64_t last = 0;
  static int64_t offset = 0;
  time_t sec;
  uint16_t ms;
  time_ms(&sec, &ms);
  int64_t t = (int64_t)sec * 1000 + ms + offset;
  if (t < (int64_t)last) {
    offset += (int64_t)last - t;
    t = (int64_t)last;
  }
  last = (uint64_t)t;
  return last;
}

// Linear progress 0..1000 of a channel (1000 when idle).
static int fx_t(int ch) {
  if (!s_fx[ch].on) return 1000;
  uint64_t el = now_ms() - s_fx[ch].start;
  return el >= s_fx[ch].dur ? 1000 : (int)(el * 1000 / s_fx[ch].dur);
}

// Eased-out progress 0..1000: fast start, gentle landing.
static int fx_k(int ch) {
  int u = 1000 - fx_t(ch);
  return 1000 - u * u / 1000 * u / 1000;
}

// Milliseconds since a channel started, or -1 when idle.
static int fx_elapsed(int ch) {
  return s_fx[ch].on ? (int)(now_ms() - s_fx[ch].start) : -1;
}

static void fx_frame(void *data) {
  s_fx_timer = NULL;
  uint64_t now = now_ms();
  bool any = false;
  for (int ch = 0; ch < FX_COUNT; ch++) {
    if (!s_fx[ch].on) continue;
    if (now - s_fx[ch].start >= s_fx[ch].dur) {
      s_fx[ch].on = false;
      if (ch == FX_BAND && s_band_closing) { s_band_closing = false; s_date_up = false; }
      if (ch == FX_THROTTLE) td_phase_end();
    } else {
      any = true;
    }
  }
  if (s_canvas) layer_mark_dirty(s_canvas);
  if (any) s_fx_timer = app_timer_register(FX_FRAME_MS, fx_frame, NULL);
}

static void fx_start(int ch, int dur) {
  s_fx[ch] = (Fx){ .start = now_ms(), .dur = dur > 0 ? dur : 1, .on = true };
  if (!s_fx_timer) s_fx_timer = app_timer_register(FX_FRAME_MS, fx_frame, NULL);
}

// Engine indices: 0-2 center triangle (0 at top, then lower right, lower left),
// 3-12 inner ring and 13-32 outer ring, each clockwise from 12 o'clock.
#define AM_ENGINE 0

// A Rearrange flick swaps the engine order (Startup <-> Dial) for a while.
static bool s_order_swapped = false;
static bool startup_order(void) {
  return (s_cfg.engine_order == ORDER_STARTUP) != s_order_swapped;
}

// Hour engines in lighting order.
// Dial: inner ring clockwise from ~1 o'clock round to the top (hour 10), then
// the two lower center engines for 11 and 12.
static const uint8_t HOUR_DIAL[12] = { 4, 5, 6, 7, 8, 9, 10, 11, 12, 3, 1, 2 };
// Startup: the two lower center engines, then the inner ring in opposite pairs.
static const uint8_t HOUR_STARTUP[12] = { 1, 2, 3, 8, 6, 11, 4, 9, 7, 12, 5, 10 };

// Minute engines in lighting order. Dial: clockwise from 12.
static const uint8_t MIN_DIAL[20] = {
  13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32 };
// Startup: opposite pairs, each pair rotated round from the last.
static const uint8_t MIN_STARTUP[20] = {
  13, 23, 18, 28, 15, 25, 20, 30, 17, 27, 22, 32, 14, 24, 19, 29, 16, 26, 21, 31 };

// The full startup sequence played on open: center three, inner pairs, outer pairs.
static uint8_t s_startup_all[ENGINES];

static void build_startup_sequence(void) {
  int n = 0;
  s_startup_all[n++] = 0;
  for (int i = 0; i < 12; i++) s_startup_all[n++] = HOUR_STARTUP[i];
  for (int i = 0; i < 20; i++) s_startup_all[n++] = MIN_STARTUP[i];
}

// Which engines are lit for the current time; *newest gets the newest minute engine.
// With minute pulsing on, each minute engine pulses slowly in the first minute
// of its three, faster in the second, and is lit in the third; *pulsing gets
// the engine to pulse, or -1.
static void time_engines(bool lit[ENGINES], int *newest, int *pulsing) {
  memset(lit, 0, ENGINES * sizeof(bool));
  *newest = -1;
  *pulsing = -1;
  bool startup = startup_order();
  lit[AM_ENGINE] = s_now.tm_hour < 12;
  int h = s_now.tm_hour % 12;
  if (h == 0) h = 12;
  const uint8_t *ho = startup ? HOUR_STARTUP : HOUR_DIAL;
  for (int i = 0; i < h; i++) lit[ho[i]] = true;
  int m = s_now.tm_min / 3;
  const uint8_t *mo = startup ? MIN_STARTUP : MIN_DIAL;
  if (s_cfg.pulse_minutes) {
    if (s_now.tm_min % 3 == 2) m++;              // third minute: lit
    else *pulsing = mo[m];                        // first or second: pulsing
  }
  for (int i = 0; i < m; i++) lit[mo[i]] = true;
  // The white-hot "newest" core marks progress; a pulsing engine already does,
  // so only show it when nothing is pulsing.
  if (m > 0 && *pulsing < 0) *newest = mo[m - 1];
}

// Engine i's center.
static GPoint engine_pos(GPoint pc, int i) {
  int ring_r, k, n;
  int32_t offset = 0;
  if (i < 3)       { ring_r = sc(19); k = i;      n = 3; }
  else if (i < 13) { ring_r = sc(51); k = i - 3;  n = 10; }
  else             { ring_r = sc(88); k = i - 13; n = 20; offset = TRIG_MAX_ANGLE / 40; }
  int32_t a = TRIG_MAX_ANGLE * k / n + offset;
  return GPoint(pc.x + sin_lookup(a) * ring_r / TRIG_MAX_RATIO,
                pc.y - cos_lookup(a) * ring_r / TRIG_MAX_RATIO);
}

static void draw_engine(GContext *ctx, GPoint p, int er, bool lit, bool newest) {
  if (lit) {
    const uint32_t *f = FLAMES[s_cfg.flame];
    graphics_context_set_fill_color(ctx, GColorFromHEX(f[0]));
    graphics_fill_circle(ctx, p, er);
    graphics_context_set_fill_color(ctx, GColorFromHEX(f[1]));
    graphics_fill_circle(ctx, p, er * 70 / 100);
    graphics_context_set_fill_color(ctx, newest ? GColorWhite : GColorFromHEX(f[2]));
    graphics_fill_circle(ctx, p, newest ? er * 50 / 100 : er * 38 / 100);
  } else {
    // A dark nozzle: bell rim and throat.
    graphics_context_set_stroke_color(ctx, GColorFromHEX(0x555555));
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_circle(ctx, p, er - 1);
    graphics_context_set_fill_color(ctx, GColorFromHEX(0x555555));
    graphics_fill_circle(ctx, p, er / 4 > 1 ? er / 4 : 1);
  }
}

// An engine igniting, u = 0..1000: the flame blooms out, a white-hot core
// swells past full size and settles to the steady core.
static void draw_flare(GContext *ctx, GPoint p, int er, int u, bool newest) {
  const uint32_t *f = FLAMES[s_cfg.flame];
  int outer = u < 300 ? er * (45 + u * 55 / 300) / 100 : er;
  graphics_context_set_fill_color(ctx, GColorFromHEX(f[0]));
  graphics_fill_circle(ctx, p, outer);
  if (u > 150) {
    graphics_context_set_fill_color(ctx, GColorFromHEX(f[1]));
    graphics_fill_circle(ctx, p, er * 70 / 100);
  }
  int final_pct = newest ? 50 : 38;
  int pct = u < 450 ? 64 * u / 450 : 64 - (64 - final_pct) * (u - 450) / 550;
  if (pct <= 0) return;
  graphics_context_set_fill_color(ctx, (u < 800 || newest) ? GColorWhite : GColorFromHEX(f[2]));
  graphics_fill_circle(ctx, p, er * pct / 100);
}

// An engine about to go dark: flame without its core.
static void draw_flameout(GContext *ctx, GPoint p, int er) {
  const uint32_t *f = FLAMES[s_cfg.flame];
  graphics_context_set_fill_color(ctx, GColorFromHEX(f[0]));
  graphics_fill_circle(ctx, p, er * 85 / 100);
  graphics_context_set_fill_color(ctx, GColorFromHEX(f[1]));
  graphics_fill_circle(ctx, p, er * 55 / 100);
}

// --- engine transitions: what changed since the last settled state, and when
// each engine ignites or shuts down within the transition.

enum { TR_NONE = 0, TR_ON, TR_OFF };
static uint8_t s_tr_kind[ENGINES];
static int16_t s_tr_ms[ENGINES];     // ignition start, or the moment it goes dark
static bool s_prev_lit[ENGINES];
static bool s_prev_valid = false;

static void time_engines(bool lit[ENGINES], int *newest, int *pulsing);

// Compare the engines for the current time with the last settled state and
// schedule the difference: shutdowns first (reverse lighting order, flaming
// out), then ignitions (lighting order, flaring).
static void engines_transition(void) {
  bool lit[ENGINES];
  int newest, pulsing;
  time_engines(lit, &newest, &pulsing);
  if (!s_prev_valid) {
    memcpy(s_prev_lit, lit, sizeof(lit));
    s_prev_valid = true;
    return;
  }
  bool startup = startup_order();
  const uint8_t *ho = startup ? HOUR_STARTUP : HOUR_DIAL;
  const uint8_t *mo = startup ? MIN_STARTUP : MIN_DIAL;
  uint8_t order[ENGINES];                        // lighting order: AM, hours, minutes
  int n = 0;
  order[n++] = AM_ENGINE;
  for (int i = 0; i < 12; i++) order[n++] = ho[i];
  for (int i = 0; i < 20; i++) order[n++] = mo[i];

  int offs = 0, ons = 0;
  for (int i = 0; i < ENGINES; i++) {
    if (s_prev_lit[i] && !lit[i]) offs++;
    if (!s_prev_lit[i] && lit[i]) ons++;
  }
  memset(s_tr_kind, 0, sizeof(s_tr_kind));
  if (offs == 0 && ons == 0) return;

  int step = offs > 1 ? OFF_SPAN_MS / (offs - 1) : 0;
  int r = 0, end = 0;
  for (int j = ENGINES - 1; j >= 0; j--) {       // shut down last-lit first
    int i = order[j];
    if (s_prev_lit[i] && !lit[i]) {
      s_tr_kind[i] = TR_OFF;
      s_tr_ms[i] = FLAMEOUT_MS + r++ * step;
      if (s_tr_ms[i] > end) end = s_tr_ms[i];
    }
  }
  int base = offs ? end + 80 : 0;
  r = 0;
  for (int j = 0; j < ENGINES; j++) {            // then ignite in lighting order
    int i = order[j];
    if (!s_prev_lit[i] && lit[i]) {
      s_tr_kind[i] = TR_ON;
      s_tr_ms[i] = base + r++ * ON_STEP_MS;
      if (s_tr_ms[i] + FLARE_MS > end) end = s_tr_ms[i] + FLARE_MS;
    }
  }
  memcpy(s_prev_lit, lit, sizeof(lit));
  fx_start(FX_ENGINES, end + FX_FRAME_MS);
}

// Draw engine i, mid-transition if it has one.
static void draw_engine_now(GContext *ctx, GPoint p, int er, int i, bool lit, bool newest) {
  int el = fx_elapsed(FX_ENGINES);
  if (el >= 0 && s_tr_kind[i] == TR_ON) {
    int st = s_tr_ms[i];
    if (el < st) draw_engine(ctx, p, er, false, false);
    else if (el < st + FLARE_MS) draw_flare(ctx, p, er, (el - st) * 1000 / FLARE_MS, newest);
    else draw_engine(ctx, p, er, true, newest);
    return;
  }
  if (el >= 0 && s_tr_kind[i] == TR_OFF) {
    int st = s_tr_ms[i];
    if (el < st - FLAMEOUT_MS) draw_engine(ctx, p, er, true, false);
    else if (el < st) draw_flameout(ctx, p, er);
    else draw_engine(ctx, p, er, false, false);
    return;
  }
  draw_engine(ctx, p, er, lit, newest);
}

// ---------------------------------------------------------------- rim timeline
//
// Drawn like a launch-broadcast mission timeline: event nodes on a thin track.
// Nodes behind the progress point are filled and joined by a heavier line;
// nodes ahead are hollow on a hairline. The progress point is a larger filled
// node with a halo.

#define MAX_NODES 31

// Point `along` out from c on angle a, shifted `across` (positive = clockwise side).
static GPoint polar2(GPoint c, int32_t a, int along, int across) {
  int32_t s = sin_lookup(a), co = cos_lookup(a);
  return GPoint(c.x + (s * along + co * across) / TRIG_MAX_RATIO,
                c.y + (-co * along + s * across) / TRIG_MAX_RATIO);
}

static GPoint rim_point(GPoint c, int32_t a, int rr) {
  return GPoint(c.x + sin_lookup(a) * rr / TRIG_MAX_RATIO,
                c.y - cos_lookup(a) * rr / TRIG_MAX_RATIO);
}

// angles: node positions, 0..TRIG_MAX_ANGLE clockwise from 12.
// progress: 0..TRIG_MAX_ANGLE.
// The rim sweeps out from 12 o'clock when it changes: 0..1000 of the way.
static int rim_k(void);

static void draw_timeline(GContext *ctx, GPoint c, const int32_t *angles, int n,
                          int32_t progress, uint32_t color) {
  int rr = sc(RIM_R);
  progress = (int32_t)((int64_t)progress * rim_k() / 1000);
  GColor on   = s_connected ? GColorFromHEX(color) : GColorFromHEX(0xAAAAAA);
  GColor dim  = s_connected ? GColorFromHEX(0xAAAAAA) : GColorFromHEX(0x555555);
  GColor hair = GColorFromHEX(0x555555);
  if (progress < 0) progress = 0;
  if (progress > TRIG_MAX_ANGLE) progress = TRIG_MAX_ANGLE;

  // Track ahead: a hairline all the way round.
  graphics_context_set_stroke_color(ctx, hair);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, c, rr);

  // Track behind: heavier line from 12 to the progress point.
  if (progress > 0) {
    int th = sc(3) < 2 ? 2 : sc(3);
    graphics_context_set_fill_color(ctx, on);
    graphics_fill_radial(ctx, GRect(c.x - rr - th / 2, c.y - rr - th / 2, 2 * rr + th, 2 * rr + th),
                         GOvalScaleModeFitCircle, th, 0, progress);
  }

  // Event nodes. Hollow ones are filled black so the line doesn't run through them.
  int nr = sc(4) < 3 ? 3 : sc(4);
  if (n > 16 && nr > 3) nr--;           // a little smaller when there are many
  int32_t marker = progress % TRIG_MAX_ANGLE;
  for (int k = 0; k < n; k++) {
    int32_t a = angles[k];
    if (a == marker) continue;          // the progress marker sits here
    GPoint p = rim_point(c, a, rr);
    if (a < progress) {
      graphics_context_set_fill_color(ctx, on);
      graphics_fill_circle(ctx, p, nr);
    } else {
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_circle(ctx, p, nr);
      graphics_context_set_stroke_color(ctx, dim);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_circle(ctx, p, nr);
    }
  }

  // Progress point: larger filled node, black gap, thin halo.
  GPoint p = rim_point(c, marker, rr);
  int cr = sc(6) < 4 ? 4 : sc(6);
  int hr = sc(HALO_R) < 7 ? 7 : sc(HALO_R);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, p, hr);
  graphics_context_set_stroke_color(ctx, on);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, p, hr);
  graphics_context_set_fill_color(ctx, on);
  graphics_fill_circle(ctx, p, cr);
}

static void even_nodes(int32_t *angles, int n) {
  for (int k = 0; k < n; k++) angles[k] = TRIG_MAX_ANGLE * k / n;
}

static int32_t frac_angle(int32_t num, int32_t den) {
  if (den <= 0) return 0;
  if (num > den) num = den;
  if (num < 0) num = 0;
  return (int32_t)((int64_t)TRIG_MAX_ANGLE * num / den);
}

// ---------------------------------------------------------------- complications

static void rim_hours(GContext *ctx, GPoint c) {
  int32_t a[12];
  even_nodes(a, 12);
  draw_timeline(ctx, c, a, 12, TRIG_MAX_ANGLE * (s_now.tm_hour % 12) / 12, 0xFFFFFF);
}

static void rim_steps(GContext *ctx, GPoint c) {
  int32_t a[10];
  even_nodes(a, 10);
  draw_timeline(ctx, c, a, 10, frac_angle(s_steps, s_cfg.step_goal), FLAMES[s_cfg.flame][1]);
}

static void rim_day(GContext *ctx, GPoint c) {
  int32_t a[4];
  int n = 0;
  a[n++] = 0;                                                   // midnight
  if (s_sun.rise != NO_TIME) a[n++] = frac_angle(s_sun.rise, DAY_MIN);
  a[n++] = frac_angle(s_sun.noon != NO_TIME ? s_sun.noon : 720, DAY_MIN);
  if (s_sun.set != NO_TIME) a[n++] = frac_angle(s_sun.set, DAY_MIN);
  int now = s_now.tm_hour * 60 + s_now.tm_min;
  draw_timeline(ctx, c, a, n, frac_angle(now, DAY_MIN), COLOR_DAY);
}

static void rim_month(GContext *ctx, GPoint c) {
  int32_t a[MAX_NODES];
  int n = days_in_month(s_now.tm_year + 1900, s_now.tm_mon);
  even_nodes(a, n);
  draw_timeline(ctx, c, a, n, TRIG_MAX_ANGLE * (s_now.tm_mday - 1) / n, COLOR_MONTH);
}

static void rim_tminus(GContext *ctx, GPoint c) {
  int32_t a[MAX_NODES];
  if (s_cfg.tminus_day == NO_DATE) {            // no date set: an empty count
    even_nodes(a, 10);
    draw_timeline(ctx, c, a, 10, 0, COLOR_TMINUS);
    return;
  }
  int32_t left = s_cfg.tminus_day - today_day();
  if (left <= 0) {                              // the day has come: full ring
    even_nodes(a, 10);
    draw_timeline(ctx, c, a, 10, TRIG_MAX_ANGLE, COLOR_TMINUS);
  } else if (left <= TERMINAL_DAYS) {           // terminal count: a node per day
    even_nodes(a, TERMINAL_DAYS);
    draw_timeline(ctx, c, a, TERMINAL_DAYS,
                  TRIG_MAX_ANGLE * (TERMINAL_DAYS - left) / TERMINAL_DAYS, COLOR_TMINUS);
  } else {                                      // long count: share of the wait elapsed
    int32_t start = s_cfg.tminus_start == NO_DATE ? today_day() : s_cfg.tminus_start;
    int32_t span = s_cfg.tminus_day - start;
    even_nodes(a, 10);
    draw_timeline(ctx, c, a, 10, frac_angle(today_day() - start, span), COLOR_TMINUS);
  }
}

// Moon age as an angle round one mean synodic month (new moon = 0). Within
// about a day of the real phases.
#define MOON_REF_NEW  947182440   // 2000-01-06 18:14 UTC, a new moon
#define MOON_SYNODIC  2551443     // 29.530589 days, in seconds

static int32_t moon_angle(time_t t) {
  int64_t age = ((int64_t)t - MOON_REF_NEW) % MOON_SYNODIC;
  if (age < 0) age += MOON_SYNODIC;
  return (int32_t)(age * TRIG_MAX_ANGLE / MOON_SYNODIC);
}

// The moon at its current phase: dim disc, lit part drawn row by row. Waxing
// lights the right side (northern hemisphere); south of the equator it flips.
static void draw_moon(GContext *ctx, GPoint p, int r, int32_t phase) {
  graphics_context_set_fill_color(ctx, GColorFromHEX(0x555555));
  graphics_fill_circle(ctx, p, r);
  int32_t k = cos_lookup(phase);                  // 1 new .. -1 full
  bool waxing = phase < TRIG_MAX_ANGLE / 2;
  bool flip = s_sun.south != 0;
  graphics_context_set_fill_color(ctx, s_connected ? GColorWhite : GColorFromHEX(0xAAAAAA));
  for (int y = -r; y <= r; y++) {
    int w = 0;
    // Half-width of this row, sized to (r + 1/2) to match how the disc rasterises.
    while ((w + 1) * (w + 1) <= r * r + r - y * y) w++;
    int t = w * k / TRIG_MAX_RATIO;                  // terminator
    int x0, x1;
    if (waxing) { x0 = t;  x1 = w; }                 // right side lit
    else        { x0 = -w; x1 = -t; }                // left side lit
    if (flip) { int tmp = x0; x0 = -x1; x1 = -tmp; }
    if (x1 > x0) graphics_fill_rect(ctx, GRect(p.x + x0, p.y + y, x1 - x0 + 1, 1), 0, GCornerNone);
  }
}

static void rim_moon(GContext *ctx, GPoint c) {
  int32_t a[4];
  even_nodes(a, 4);                               // new, first quarter, full, last quarter
  int32_t phase = moon_angle(time(NULL));
  draw_timeline(ctx, c, a, 4, phase, COLOR_MOON);
  // Replace the plain marker with the moon itself, a little larger. It rides
  // the sweep; its shape is always tonight's phase.
  int rr = sc(RIM_R);
  GPoint p = rim_point(c, (int32_t)((int64_t)phase * rim_k() / 1000) % TRIG_MAX_ANGLE, rr);
  int hr = sc(BODY_HALO) < 8 ? 8 : sc(BODY_HALO);
  int mr = sc(8) < 6 ? 6 : sc(8);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, p, hr + 1);
  graphics_context_set_stroke_color(ctx, s_connected ? GColorFromHEX(COLOR_MOON) : GColorFromHEX(0xAAAAAA));
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, p, hr);
  draw_moon(ctx, p, mr, phase);
}

// --- Moon & Mars distance

#define MOON_NEAR_KM   356400
#define MOON_FAR_KM    406700
#define MARS_NEAR_MAU  372
#define MARS_FAR_MAU   2680

// Distance -> rim angle: 0 at the top (closest) to half way round (farthest),
// on the left when approaching and the right when moving away.
static int32_t distance_angle(int32_t d, int32_t nearest, int32_t farthest, bool approaching) {
  int32_t off = frac_angle(d - nearest, farthest - nearest) / 2;
  off = (int32_t)((int64_t)off * rim_k() / 1000);              // rides the sweep
  return approaching ? TRIG_MAX_ANGLE - off : off;
}

// Arc from 12 o'clock to angle a along the shorter side, at radius r.
static void side_arc(GContext *ctx, GPoint c, int r, int th, int32_t a, GColor color) {
  graphics_context_set_fill_color(ctx, color);
  GRect rect = GRect(c.x - r - th / 2, c.y - r - th / 2, 2 * r + th, 2 * r + th);
  if (a <= TRIG_MAX_ANGLE / 2) {
    if (a > 0) graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, th, 0, a);
  } else {
    graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, th, a, TRIG_MAX_ANGLE);
  }
}

static void draw_mars(GContext *ctx, GPoint p, int r) {
  graphics_context_set_fill_color(ctx, s_connected ? GColorFromHEX(COLOR_MARS) : GColorFromHEX(0xAAAAAA));
  graphics_fill_circle(ctx, p, r);
  int cap = r / 3 > 1 ? r / 3 : 1;                 // a hint of polar ice
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(p.x, p.y - r + cap), cap);
}

static void rim_orbits(GContext *ctx, GPoint c) {
  int rr = sc(RIM_R);
  GColor hair = GColorFromHEX(0x555555);
  GColor dim = s_connected ? GColorFromHEX(0xAAAAAA) : GColorFromHEX(0x555555);

  graphics_context_set_stroke_color(ctx, hair);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, c, rr);

  // Scale: small dots at each eighth, hollow node at the far point.
  graphics_context_set_fill_color(ctx, hair);
  for (int k = 1; k < 8; k++) {
    if (k == 4) continue;
    graphics_fill_circle(ctx, rim_point(c, TRIG_MAX_ANGLE * k / 8, rr), sc(2) < 1 ? 1 : sc(2));
  }
  int nr = sc(4) < 3 ? 3 : sc(4);
  GPoint far = rim_point(c, TRIG_MAX_ANGLE / 2, rr);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, far, nr);
  graphics_context_set_stroke_color(ctx, dim);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_circle(ctx, far, nr);
  // Earth at the near point.
  graphics_context_set_fill_color(ctx, s_connected ? GColorFromHEX(COLOR_EARTH) : GColorFromHEX(0xAAAAAA));
  graphics_fill_circle(ctx, rim_point(c, 0, rr), nr + 1);

  bool have_moon = s_orb.moon_km > 0, have_mars = s_orb.mars_mau > 0;
  int32_t am = have_moon ? distance_angle(s_orb.moon_km, MOON_NEAR_KM, MOON_FAR_KM, s_orb.moon_in) : 0;
  int32_t ar = have_mars ? distance_angle(s_orb.mars_mau, MARS_NEAR_MAU, MARS_FAR_MAU, s_orb.mars_in) : 0;

  // Arcs end at the true positions: Moon just inside the track, Mars just outside.
  int th = sc(2) < 2 ? 2 : sc(2);
  GColor moon_c = s_connected ? GColorFromHEX(COLOR_MOON) : GColorFromHEX(0xAAAAAA);
  GColor mars_c = s_connected ? GColorFromHEX(COLOR_MARS) : GColorFromHEX(0xAAAAAA);
  if (have_moon) side_arc(ctx, c, rr - sc(3), th, am, moon_c);
  if (have_mars) side_arc(ctx, c, rr + sc(3), th, ar, mars_c);

  // Markers: if they would overlap, spread them apart around their midpoint.
  int hr = sc(BODY_HALO) < 8 ? 8 : sc(BODY_HALO);
  int32_t pm = am, pr = ar;
  if (have_moon && have_mars) {
    const int32_t min_sep = TRIG_MAX_ANGLE * 15 / 360;
    int32_t diff = pr - pm;
    if (diff > TRIG_MAX_ANGLE / 2) diff -= TRIG_MAX_ANGLE;
    if (diff < -TRIG_MAX_ANGLE / 2) diff += TRIG_MAX_ANGLE;
    if (diff > -min_sep && diff < min_sep) {
      int32_t mid = pm + diff / 2;
      int32_t half = min_sep / 2;
      if (diff >= 0) { pm = mid - half; pr = mid + half; }
      else           { pm = mid + half; pr = mid - half; }
      pm = (pm + TRIG_MAX_ANGLE) % TRIG_MAX_ANGLE;
      pr = (pr + TRIG_MAX_ANGLE) % TRIG_MAX_ANGLE;
    }
  }
  if (have_moon) {
    GPoint p = rim_point(c, pm, rr);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, p, hr + 1);
    graphics_context_set_stroke_color(ctx, moon_c);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, p, hr);
    draw_moon(ctx, p, sc(8) < 6 ? 6 : sc(8), moon_angle(time(NULL)));
  }
  if (have_mars) {
    GPoint p = rim_point(c, pr, rr);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, p, hr + 1);
    graphics_context_set_stroke_color(ctx, mars_c);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_circle(ctx, p, hr);
    draw_mars(ctx, p, sc(7) < 5 ? 5 : sc(7));
  }
}

// --- Seconds: a seconds hand on the rim. The face only ticks every second
// while this is on screen.
static void rim_seconds(GContext *ctx, GPoint c) {
  int32_t a[12];
  even_nodes(a, 12);
  draw_timeline(ctx, c, a, 12, TRIG_MAX_ANGLE * s_now.tm_sec / 60, COLOR_SECONDS);
}

static bool rim_enabled(int mode) {
  switch (mode) {
    case RIM_HOURS:  return true;
    case RIM_STEPS:  return s_cfg.show_steps;
    case RIM_DAY:    return s_cfg.show_day;
    case RIM_MONTH:  return s_cfg.show_month;
    case RIM_TMINUS: return s_cfg.show_tminus;
    case RIM_MOON:   return s_cfg.show_moon;
    case RIM_ORBITS: return s_cfg.show_orbits;
    case RIM_SECONDS: return s_cfg.show_seconds;
    default:         return false;
  }
}

static void draw_rim(GContext *ctx, GPoint c) {
  switch (s_showing >= 0 ? s_showing : s_cfg.rim) {
    case RIM_STEPS:  rim_steps(ctx, c); break;
    case RIM_DAY:    rim_day(ctx, c); break;
    case RIM_MONTH:  rim_month(ctx, c); break;
    case RIM_TMINUS: rim_tminus(ctx, c); break;
    case RIM_MOON:   rim_moon(ctx, c); break;
    case RIM_ORBITS: rim_orbits(ctx, c); break;
    case RIM_SECONDS: rim_seconds(ctx, c); break;
    default:         rim_hours(ctx, c); break;
  }
}

// ---------------------------------------------------------------- date readout
//
// A broadcast-style lower third across the engine plate: day and date, with
// the day of the year beneath, between thin rules.

static const char *DAYS[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
static const char *MONTHS[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

static void draw_date_readout(GContext *ctx, GPoint c) {
  bool big = s_r >= 110;
  int full_w = sc(124), h = big ? 44 : 32;
  // Opens outward from the center and closes back into it.
  int k = s_fx[FX_BAND].on ? fx_k(FX_BAND) : 1000;
  int open = s_band_closing ? 1000 - k : k;
  int w = full_w * open / 1000;
  if (w < 4) return;
  GRect band = GRect(c.x - w / 2, c.y - h / 2, w, h);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, band, 3, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorFromHEX(0xAAAAAA));
  graphics_context_set_stroke_width(ctx, 1);
  int inset = sc(8);
  graphics_draw_line(ctx, GPoint(band.origin.x + inset, band.origin.y),
                          GPoint(band.origin.x + w - inset, band.origin.y));
  graphics_draw_line(ctx, GPoint(band.origin.x + inset, band.origin.y + h - 1),
                          GPoint(band.origin.x + w - inset, band.origin.y + h - 1));
  if (open < 800) return;                          // text once the band is (nearly) open

  char line1[16], line2[32];
  int year = s_now.tm_year + 1900;
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  snprintf(line1, sizeof(line1), "%s %d %s", DAYS[s_now.tm_wday % 7], s_now.tm_mday,
           MONTHS[s_now.tm_mon % 12]);
  snprintf(line2, sizeof(line2), "DAY %d OF %d", s_now.tm_yday + 1, leap ? 366 : 365);
  GFont f1 = fonts_get_system_font(big ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD);
  GFont f2 = fonts_get_system_font(big ? FONT_KEY_GOTHIC_14 : FONT_KEY_GOTHIC_09);
  int h1 = big ? 18 : 14;
  int y = band.origin.y + (big ? -1 : -2);
  graphics_context_set_text_color(ctx, s_connected ? GColorWhite : GColorFromHEX(0xAAAAAA));
  graphics_draw_text(ctx, line1, f1, GRect(band.origin.x, y, w, h1 + 6),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, GColorFromHEX(0xAAAAAA));
  graphics_draw_text(ctx, line2, f2, GRect(band.origin.x, y + h1 + (big ? 3 : 1), w, h1 + 4),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}


// ---------------------------------------------------------------- throttle-down readout
//
// The other flick readout. A flick throttles the lit engines down to embers and
// the time and date glow over them. When the flick ends the engines relight in
// startup order; once every one is back at full, their heat works on the
// lettering: white, then amber, orange and dull red, then charred black with a
// glowing rim, and finally it crumbles into embers that drift up and cool to ash.

enum { TD_OFF, TD_DOWN, TD_HOLD, TD_UP };
#define TD_DOWN_MS      350   // engines throttle to embers, lettering glows up
#define TD_RELIGHT_STEP 25    // between engines relighting, in startup order
#define TD_HEAT_AT      1300  // into TD_UP: every engine is back at full
#define TD_SPREAD       2     // heat reaches the lettering's outer parts later (ms per px)
#define TD_JITTER       31    // ...and each pixel up to this much later, so it looks organic
#define TD_AMBER        140   // heat ages: white -> yellow -> amber -> orange -> red -> char
#define TD_ORANGE       280
#define TD_RED          420
#define TD_CHAR         560
#define TD_CRUMBLE      900   // charred for a beat, then it crumbles...
#define TD_CRUMBLE_MS   600   // ...over this long
#define TD_ASH_MS       700   // how long a fleck drifts
#define TD_UP_MS        (TD_HEAT_AT + 140 * TD_SPREAD + TD_JITTER + TD_CRUMBLE + TD_CRUMBLE_MS + TD_ASH_MS)

// Embers for each flame: throttling down, then throttled (outer, mid, core).
static const uint32_t EMBERS[FLAME_COUNT][2][3] = {
  { { 0xAA0000, 0xFF5500, 0xFF5500 }, { 0x550000, 0x550000, 0xAA0000 } },
  { { 0x0000AA, 0x5500AA, 0xAA55FF }, { 0x000055, 0x000055, 0x5500AA } },
  { { 0x555555, 0xAAAAAA, 0xAAAAAA }, { 0x555555, 0x555555, 0xAAAAAA } },
};
static const uint32_t TD_DATE[FLAME_COUNT] = { 0xFFAA55, 0xAAAAFF, 0xAAAAAA };

static uint8_t s_td = TD_OFF;
static int s_td_el;                  // ms into the current phase, fixed for one frame
static uint8_t s_td_order[ENGINES];  // each engine's place in the startup sequence
static uint8_t *s_td_mask;           // two bit planes over the text box: lettering, date
static GRect s_td_box;
static bool s_td_mask_ok;
static void update_pulse(void);

static void td_layout(GPoint c, GRect *tr, GRect *dr, GFont *tf, GFont *df, GRect *box) {
  if (s_r >= 110) {
    *tf = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
    *df = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    *tr = GRect(c.x - 80, c.y - 34, 160, 50);
    *dr = GRect(c.x - 60, c.y + 16, 120, 24);
    *box = GRect(c.x - 84, c.y - 40, 168, 84);
  } else {
    *tf = fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS);
    *df = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    *tr = GRect(c.x - 60, c.y - 28, 120, 40);
    *dr = GRect(c.x - 50, c.y + 9, 100, 18);
    *box = GRect(c.x - 62, c.y - 30, 124, 62);
  }
}

static void td_strings(char *t, int tn, char *d, int dn) {
  int h = s_now.tm_hour;
  if (!clock_is_24h_style()) { h %= 12; if (h == 0) h = 12; }
  snprintf(t, tn, clock_is_24h_style() ? "%02d:%02d" : "%d:%02d", h, s_now.tm_min);
  snprintf(d, dn, "%s %d %s", DAYS[s_now.tm_wday % 7], s_now.tm_mday, MONTHS[s_now.tm_mon % 12]);
}

static void td_open(void) {
  if (s_td == TD_DOWN || s_td == TD_HOLD) return;
  for (int j = 0; j < ENGINES; j++) s_td_order[s_startup_all[j]] = j;
  s_td = TD_DOWN;
  s_td_mask_ok = false;
  fx_start(FX_THROTTLE, TD_DOWN_MS);
  update_pulse();                     // the pulse waits while the engines are throttled
}

static void td_close(void) {
  if (s_td != TD_DOWN && s_td != TD_HOLD) return;
  s_td = TD_UP;
  s_td_mask_ok = false;
  fx_start(FX_THROTTLE, TD_UP_MS);
}

static void td_phase_end(void) {
  if (s_td == TD_DOWN) {
    s_td = TD_HOLD;                   // still until the flick ends: no frames needed
  } else if (s_td == TD_UP) {
    s_td = TD_OFF;
    if (s_td_mask) { free(s_td_mask); s_td_mask = NULL; }
    s_td_mask_ok = false;
    update_pulse();
  }
}

// A lit engine while the readout is up: an ember, or relighting.
static void td_draw_engine(GContext *ctx, GPoint p, int er, int i, bool newest) {
  int stage = 1;                      // 0 throttling down, 1 throttled
  if (s_td == TD_DOWN && s_td_el < TD_DOWN_MS / 2) stage = 0;
  if (s_td == TD_UP) {
    int at = s_td_order[i] * TD_RELIGHT_STEP;
    if (s_td_el >= at + FLARE_MS) { draw_engine(ctx, p, er, true, newest); return; }
    if (s_td_el >= at) { draw_flare(ctx, p, er, (s_td_el - at) * 1000 / FLARE_MS, newest); return; }
  }
  const uint32_t *e = EMBERS[s_cfg.flame][stage];
  graphics_context_set_fill_color(ctx, GColorFromHEX(e[0]));
  graphics_fill_circle(ctx, p, er);
  graphics_context_set_fill_color(ctx, GColorFromHEX(e[1]));
  graphics_fill_circle(ctx, p, er * 70 / 100);
  graphics_context_set_fill_color(ctx, GColorFromHEX(e[2]));
  graphics_fill_circle(ctx, p, er * 38 / 100);
}

static int td_noise(int x, int y) {
  uint32_t h = (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663);
  h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;
  return h & 255;
}

static int td_bits(void) { return s_td_box.size.w * s_td_box.size.h; }
static bool td_letter(int x, int y) {
  if (!grect_contains_point(&s_td_box, &GPoint(x, y))) return false;
  int i = (y - s_td_box.origin.y) * s_td_box.size.w + (x - s_td_box.origin.x);
  return s_td_mask[i >> 3] & (1 << (i & 7));
}
static bool td_is_date(int x, int y) {
  int i = (y - s_td_box.origin.y) * s_td_box.size.w + (x - s_td_box.origin.x);
  return s_td_mask[(td_bits() + 7) / 8 + (i >> 3)] & (1 << (i & 7));
}

// The lettering's shape, read back from the frame buffer: drawn in marker
// colours on the empty canvas before the engines, recorded, then cleared.
static void td_build_mask(GContext *ctx, GPoint c) {
  GRect tr, dr, box; GFont tf, df;
  td_layout(c, &tr, &dr, &tf, &df, &box);
  if (!s_td_mask || !grect_equal(&box, &s_td_box)) {
    if (s_td_mask) free(s_td_mask);
    s_td_box = box;
    s_td_mask = malloc((td_bits() + 7) / 8 * 2);
    if (!s_td_mask) return;
  }
  memset(s_td_mask, 0, (td_bits() + 7) / 8 * 2);
  char t[8], d[16];
  td_strings(t, sizeof(t), d, sizeof(d));
  graphics_context_set_text_color(ctx, GColorMagenta);
  graphics_draw_text(ctx, t, tf, tr, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, GColorCyan);
  graphics_draw_text(ctx, d, df, dr, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  int planes = (td_bits() + 7) / 8;
  for (int y = box.origin.y; y < box.origin.y + box.size.h; y++) {
    GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, y);
    int x0 = box.origin.x > ri.min_x ? box.origin.x : ri.min_x;
    int x1 = box.origin.x + box.size.w - 1 < ri.max_x ? box.origin.x + box.size.w - 1 : ri.max_x;
    for (int x = x0; x <= x1; x++) {
      uint8_t v = ri.data[x];
      if (v != GColorMagentaARGB8 && v != GColorCyanARGB8) continue;
      int i = (y - box.origin.y) * box.size.w + (x - box.origin.x);
      s_td_mask[i >> 3] |= 1 << (i & 7);
      if (v == GColorCyanARGB8) s_td_mask[planes + (i >> 3)] |= 1 << (i & 7);
      ri.data[x] = GColorBlackARGB8;
    }
  }
  graphics_release_frame_buffer(ctx, fb);
  s_td_mask_ok = true;
}

// Heat age of one pixel of lettering: ms since the heat reached it.
static int td_age(GPoint c, int x, int y) {
  int d = (abs(x - c.x) + abs(y - c.y)) * 3 / 4;
  return s_td_el - TD_HEAT_AT - d * TD_SPREAD - (td_noise(x, y) & TD_JITTER);
}
static int td_crumble_at(int x, int y) { return TD_CRUMBLE + td_noise(y, x) * TD_CRUMBLE_MS / 256; }
static bool td_gone(GPoint c, int x, int y) {
  return !td_letter(x, y) || td_age(c, x, y) >= td_crumble_at(x, y);
}

static void td_draw_text(GContext *ctx, GPoint c) {
  bool heating = s_td == TD_UP && s_td_el >= TD_HEAT_AT && s_td_mask_ok;
  if (!heating) {                     // plain lettering, glowing up as the engines drop
    int k = s_td == TD_DOWN ? 1 + s_td_el * 3 / TD_DOWN_MS : 3;
    if (k > 3) k = 3;
    GRect tr, dr, box; GFont tf, df;
    td_layout(c, &tr, &dr, &tf, &df, &box);
    char t[8], d[16];
    td_strings(t, sizeof(t), d, sizeof(d));
    static const uint32_t TIME_K[3] = { 0x555555, 0xAAAAAA, 0xFFFFFF };
    static const uint32_t DATE_K[2] = { 0x550000, 0xAA5500 };
    graphics_context_set_text_color(ctx, GColorFromHEX(TIME_K[k - 1]));
    graphics_draw_text(ctx, t, tf, tr, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    graphics_context_set_text_color(ctx, GColorFromHEX(k == 3 ? TD_DATE[s_cfg.flame] : DATE_K[k - 1]));
    graphics_draw_text(ctx, d, df, dr, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    return;
  }
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  GRect fbb = gbitmap_get_bounds(fb);
  const uint8_t white = GColorWhiteARGB8, yellow = GColorFromHEX(0xFFFFAA).argb,
                amber = GColorFromHEX(0xFFAA00).argb, orange = GColorFromHEX(0xFF5500).argb,
                red = GColorFromHEX(0xAA0000).argb, dark = GColorFromHEX(0x550000).argb,
                ash = GColorFromHEX(0x555555).argb, date_c = GColorFromHEX(TD_DATE[s_cfg.flame]).argb;
  GRect b = s_td_box;
  for (int y = b.origin.y; y < b.origin.y + b.size.h; y++) {
    GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, y);
    for (int x = b.origin.x; x < b.origin.x + b.size.w; x++) {
      if (x < ri.min_x || x > ri.max_x || !td_letter(x, y)) continue;
      int age = td_age(c, x, y);
      int since = age - td_crumble_at(x, y);
      if (since >= 0) {
        // Crumbled: the flames show through. A quarter of the pixels drift off
        // as embers, lifted by the heat and cooling to ash.
        if ((td_noise(x, y) & 3) == 0 && since < TD_ASH_MS) {
          int dx = x - c.x, dy = y - c.y, dist = abs(dx) + abs(dy) + 1;
          int len = since * (2 + (td_noise(y, x) & 3)) / 40;
          int px = x + dx * len / dist + ((td_noise(x + 7, y) & 7) - 3) * since / 160;
          int py = y + dy * len / (2 * dist) - since / 18;
          if (py >= 0 && py < fbb.size.h) {
            GBitmapDataRowInfo pr = gbitmap_get_data_row_info(fb, py);
            if (px >= pr.min_x && px <= pr.max_x)
              pr.data[px] = since < 120 ? orange : since < 300 ? red : since < 520 ? ash : dark;
          }
        }
        continue;
      }
      uint8_t col;
      if (age < 0) col = td_is_date(x, y) ? date_c : white;
      else if (age < TD_AMBER) col = td_is_date(x, y) ? amber : yellow;
      else if (age < TD_ORANGE) col = amber;
      else if (age < TD_RED) col = orange;
      else if (age < TD_CHAR) col = red;
      else {                          // charred black, its edges still glowing
        bool edge = td_gone(c, x - 1, y) || td_gone(c, x + 1, y) ||
                    td_gone(c, x, y - 1) || td_gone(c, x, y + 1);
        col = !edge ? GColorBlackARGB8 : ((td_noise(x, y) + s_td_el / 66) & 3) ? orange : amber;
      }
      ri.data[x] = col;
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

// ---------------------------------------------------------------- maracas
//
// Easter egg for Mars's closest approach: a nod to a well-known photo of a pair
// of maracas. Two crossed, striped maracas fill the puck.

static bool maracas_time(void) {
  return s_orb.mars_closest_day > 0 && today_day() == s_orb.mars_closest_day &&
         s_now.tm_hour == 0;
}

static void draw_maraca(GContext *ctx, GPoint pivot, int32_t a, uint32_t head,
                        uint32_t stripe1, uint32_t stripe2) {
  // Handle.
  GPoint handle[4] = {
    polar2(pivot, a, -sc(27), -sc(4)), polar2(pivot, a, sc(25), -sc(4)),
    polar2(pivot, a, sc(25), sc(4)), polar2(pivot, a, -sc(27), sc(4)),
  };
  GPath hp = { .num_points = 4, .points = handle, .rotation = 0, .offset = GPointZero };
  graphics_context_set_fill_color(ctx, GColorFromHEX(0xAA5500));
  gpath_draw_filled(ctx, &hp);
  graphics_context_set_fill_color(ctx, GColorFromHEX(0xFFAA55));
  graphics_fill_circle(ctx, polar2(pivot, a, -sc(27), 0), sc(4));

  // Head: an ellipse along the handle's axis.
  int along = sc(49), rx = sc(19), ry = sc(26);
  GPoint hc = polar2(pivot, a, along, 0);
  GPoint pts[20];
  for (int k = 0; k < 20; k++) {
    int32_t t = TRIG_MAX_ANGLE * k / 20;
    pts[k] = polar2(hc, a, ry * sin_lookup(t) / TRIG_MAX_RATIO, rx * cos_lookup(t) / TRIG_MAX_RATIO);
  }
  GPath ep = { .num_points = 20, .points = pts, .rotation = 0, .offset = GPointZero };
  graphics_context_set_fill_color(ctx, GColorFromHEX(head));
  gpath_draw_filled(ctx, &ep);

  // Two bands across the head.
  int offs[2] = { -sc(7), sc(9) };
  uint32_t cols[2] = { stripe1, stripe2 };
  graphics_context_set_stroke_width(ctx, sc(5) < 2 ? 2 : sc(5));
  for (int i = 0; i < 2; i++) {
    int o = offs[i];
    int half = 0;                                    // chord half-width at this offset
    while ((half + 1) * (half + 1) * ry * ry <= rx * rx * (ry * ry - o * o)) half++;
    half -= sc(3);
    if (half <= 0) continue;
    graphics_context_set_stroke_color(ctx, GColorFromHEX(cols[i]));
    graphics_draw_line(ctx, polar2(hc, a, o, -half), polar2(hc, a, o, half));
  }
}

static void draw_maracas(GContext *ctx, GPoint c) {
  GPoint pivot = GPoint(c.x, c.y + sc(36));
  // Shake: three swings that die away, the two maracas in opposite phase.
  int32_t wobble = 0;
  if (s_fx[FX_SHAKE].on) {
    int t = fx_t(FX_SHAKE);
    int32_t amp = TRIG_MAX_ANGLE * 14 / 360 * (1000 - t) / 1000;
    wobble = amp * sin_lookup(TRIG_MAX_ANGLE * 3 * t / 1000) / TRIG_MAX_RATIO;
  }
  int32_t tilt = TRIG_MAX_ANGLE * 30 / 360;
  draw_maraca(ctx, pivot, -tilt + wobble, 0xFFAA00, 0xFF0055, 0x00AA55);
  draw_maraca(ctx, pivot, tilt - wobble, 0xFF0055, 0xFFFF55, 0x00AAFF);
}

// ---------------------------------------------------------------- drawing

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  s_r = (bounds.size.w < bounds.size.h ? bounds.size.w : bounds.size.h) / 2;
  GPoint c = grect_center_point(&bounds);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Throttle-down readout: one clock reading per frame, and the lettering's
  // shape captured before anything is drawn over it once it starts to burn.
  s_td_el = fx_elapsed(FX_THROTTLE);
  if (s_td_el < 0) s_td_el = s_td == TD_UP ? TD_UP_MS : TD_DOWN_MS;
  if (s_td == TD_UP && s_td_el >= TD_HEAT_AT && !s_td_mask_ok) td_build_mask(ctx, c);
  bool throttled = s_td == TD_DOWN || s_td == TD_HOLD;

  // Engines: the startup sequence on open, then the time.
  bool lit[ENGINES];
  int newest = -1, pulsing = -1;
  bool maracas = false;
  bool starting = s_anim_step >= 0;     // the startup sequence is running
  int start_el = -1;                     // ms into it; -1 before the first engine
  if (starting) {
    memset(lit, 0, sizeof(lit));
    if (s_anim_step > 0) start_el = (int)(now_ms() - s_startup_t0);
  } else {
    time_engines(lit, &newest, &pulsing);
    maracas = maracas_time();
  }
  // Thrust puck edge: the band seen from below between the inner 13 engines
  // (hour, AM/PM) and the outer 20 (minutes).
  graphics_context_set_stroke_color(ctx, GColorFromHEX(throttled ? 0x555555 : 0xAAAAAA));
  graphics_context_set_stroke_width(ctx, sc(2) < 1 ? 1 : sc(2));
  graphics_draw_circle(ctx, c, sc(70));

  int er = sc(12);
  if (starting) {
    // Startup: each engine flares as its turn comes, so the ignition ripples.
    for (int j = 0; j < ENGINES; j++) {
      int i = s_startup_all[j], at = j * ANIM_MS;
      GPoint p = engine_pos(c, i);
      if (start_el < at) draw_engine(ctx, p, er, false, false);
      else if (start_el < at + FLARE_MS) draw_flare(ctx, p, er, (start_el - at) * 1000 / FLARE_MS, false);
      else draw_engine(ctx, p, er, true, false);
    }
  } else {
    for (int i = maracas ? 13 : 0; i < ENGINES; i++) {   // maracas replace the inner 13
      GPoint p = engine_pos(c, i);
      // The pulse layer waits while the readout is up, so its engine is drawn here.
      if (s_td != TD_OFF && (lit[i] || i == pulsing)) { td_draw_engine(ctx, p, er, i, i == newest); continue; }
      if (i == pulsing) continue;                      // drawn by the pulse layer
      draw_engine_now(ctx, p, er, i, lit[i], i == newest);
    }
  }
  if (maracas) draw_maracas(ctx, c);

  draw_rim(ctx, c);
  if (s_date_up) draw_date_readout(ctx, c);
  if (s_td != TD_OFF) td_draw_text(ctx, c);
}

// ---------------------------------------------------------------- minute pulse
//
// The pulsing engine lives on its own small layer so only it redraws.

#define PULSE_SLOW_MS 600   // 6 frames: a 3.6 s breath in the first minute
#define PULSE_FAST_MS 220   // about 1.3 s in the second minute

static Layer *s_pulse_layer;
static AppTimer *s_pulse_timer;
static int s_pulse_engine = -1;
static int s_pulse_frame = 0;
static bool s_focused = true;

// The engine burns steady; its core breathes from nothing (the orange mid ring
// shows through) up to a large white-hot core. The range is deliberately wide:
// small steps in size or pale-vs-white are hard to see on the real screen.
static void pulse_draw(Layer *layer, GContext *ctx) {
  static const int core_pct[6] = { 0, 30, 58, 58, 30, 0 };
  GRect b = layer_get_bounds(layer);
  GPoint p = grect_center_point(&b);
  int er = sc(12);
  const uint32_t *f = FLAMES[s_cfg.flame];
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, p, er + 1);
  graphics_context_set_fill_color(ctx, GColorFromHEX(f[0]));
  graphics_fill_circle(ctx, p, er);
  graphics_context_set_fill_color(ctx, GColorFromHEX(f[1]));
  graphics_fill_circle(ctx, p, er * 70 / 100);
  int pct = core_pct[s_pulse_frame % 6];
  if (pct == 0) return;
  graphics_context_set_fill_color(ctx, pct >= 50 ? GColorWhite : GColorFromHEX(f[2]));
  graphics_fill_circle(ctx, p, er * pct / 100);
}

static void pulse_tick(void *data) {
  s_pulse_frame++;
  layer_mark_dirty(s_pulse_layer);
  s_pulse_timer = app_timer_register(s_now.tm_min % 3 == 0 ? PULSE_SLOW_MS : PULSE_FAST_MS,
                                     pulse_tick, NULL);
}

// Start, move or stop the pulse to match the time, settings and focus.
static void update_pulse(void) {
  if (!s_pulse_layer) return;
  bool lit[ENGINES];
  int newest, pulsing = -1;
  if (s_anim_step < 0 && s_focused && s_td == TD_OFF) time_engines(lit, &newest, &pulsing);
  if (pulsing < 0) {
    if (s_pulse_timer) { app_timer_cancel(s_pulse_timer); s_pulse_timer = NULL; }
    layer_set_hidden(s_pulse_layer, true);
    s_pulse_engine = -1;
    return;
  }
  if (pulsing != s_pulse_engine) {
    GRect root = layer_get_bounds(window_get_root_layer(s_window));
    GPoint p = engine_pos(grect_center_point(&root), pulsing);
    int box = sc(12) + 2;
    layer_set_frame(s_pulse_layer, GRect(p.x - box, p.y - box, 2 * box + 1, 2 * box + 1));
    s_pulse_engine = pulsing;
    s_pulse_frame = 0;
  }
  layer_set_hidden(s_pulse_layer, false);
  if (!s_pulse_timer) s_pulse_timer = app_timer_register(PULSE_SLOW_MS, pulse_tick, NULL);
  layer_mark_dirty(s_pulse_layer);
}

static void focus_handler(bool in_focus) {
  s_focused = in_focus;                 // pause while a notification covers the face
  update_pulse();
  update_tick_subscription();
}

// ---------------------------------------------------------------- startup sequence

static void anim_step(void *data) {
  s_anim = NULL;
  if (s_anim_step == 0) {
    s_startup_t0 = now_ms();
    fx_start(FX_STARTUP, ENGINES * ANIM_MS + FLARE_MS);   // keeps frames coming for the flares
  }
  if (s_anim_step < ENGINES) {
    s_anim_step++;
    s_anim = app_timer_register(s_anim_step < ENGINES ? ANIM_MS : HOLD_MS, anim_step, NULL);
  } else {
    // Sequence done: the engines not needed for the time shut down in turn,
    // and the rim sweeps in.
    s_anim_step = -1;
    memset(s_prev_lit, 1, sizeof(s_prev_lit));
    s_prev_valid = true;
    engines_transition();
    fx_start(FX_RIM, RIM_SWEEP_MS);
    if (maracas_time()) fx_start(FX_SHAKE, SHAKE_MS);
    update_pulse();
  }
  layer_mark_dirty(s_canvas);
}

static int rim_k(void) {
  if (s_anim_step >= 0) return 0;      // the rim waits for the startup sequence
  return fx_k(FX_RIM);
}

// ---------------------------------------------------------------- data sources

static void update_steps(void) {
#if defined(PBL_HEALTH)
  HealthServiceAccessibilityMask mask =
    health_service_metric_accessible(HealthMetricStepCount, time_start_of_today(), time(NULL));
  if (mask & HealthServiceAccessibilityMaskAvailable) {
    s_steps = (int32_t)health_service_sum_today(HealthMetricStepCount);
  }
#endif
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventMovementUpdate || event == HealthEventSignificantUpdate) {
    update_steps();
    if ((s_showing >= 0 ? s_showing : s_cfg.rim) == RIM_STEPS) layer_mark_dirty(s_canvas);
  }
}
#endif

// Ask the phone for data. Bit 1 = sun times (uses location), bit 2 = Moon/Mars
// distances. At most once an hour, so a phone without location access isn't
// asked over and over.
#define NEED_SUN   1
#define NEED_ORBIT 2
#define REQUEST_GAP_S (60 * 60)
static time_t s_last_request = 0;

static void request_data(int need) {
  if (!s_connected || need == 0) return;
  time_t now = time(NULL);
  if (s_last_request && now - s_last_request < REQUEST_GAP_S) return;
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) == APP_MSG_OK) {
    dict_write_uint8(out, MESSAGE_KEY_RequestSun, need);
    app_message_outbox_send();
    s_last_request = now;
  }
}

// ---------------------------------------------------------------- flick to cycle

static int shown_rim(void) { return s_showing >= 0 ? s_showing : s_cfg.rim; }

// How long a flick keeps another complication up.
static uint32_t swap_ms(void) { return (uint32_t)s_cfg.flick_secs * 1000; }

static void close_band(void) {
  if (s_date_up && !s_band_closing) {
    s_band_closing = true;
    fx_start(FX_BAND, BAND_CLOSE_MS);
  }
}

// The flick readout: the date band, or the throttle-down.
static void readout_close(void) {
  close_band();
  td_close();
}

static void readout_open(void) {
  if (s_cfg.readout == READOUT_THROTTLE) {
    close_band();
    td_open();
  } else {
    td_close();
    if (!s_date_up || s_band_closing) {           // open (or reopen) the band
      s_date_up = true;
      s_band_closing = false;
      fx_start(FX_BAND, BAND_OPEN_MS);
    }
  }
}

static void swap_timeout(void *data) {
  s_swap_timer = NULL;
  if (s_order_swapped) {                       // engines slide back to their order
    s_order_swapped = false;
    engines_transition();
    update_pulse();
  }
  int before = shown_rim();
  s_showing = -1;
  if (shown_rim() != before) fx_start(FX_RIM, RIM_SWEEP_MS);
  readout_close();
  update_tick_subscription();
  layer_mark_dirty(s_canvas);
}

// Cycle mode: each flick steps to the next enabled complication after the one
// showing; stepping past the last one returns to the default.
// One mode: a flick shows the chosen complication; another flick while it is
// up returns to the default.
// With Show date on, the date readout is up for as long as the flick is; with
// Rearrange on, the engine order is swapped (Startup <-> Dial) for as long too.
static void flick_action(void) {
  // One gesture can arrive twice (a firm screen tap also jolts the
  // accelerometer; a flick can report two taps): ignore repeats.
  static uint64_t last = 0;
  uint64_t now = now_ms();
  if (now - last < 400) return;
  last = now;
  // One mode set to the default rim, with the date and rearranging off, has
  // nothing to show.
  if (s_cfg.flick_mode == FLICK_ONE && s_cfg.flick_target == s_cfg.rim &&
      !s_cfg.show_date && !s_cfg.flick_rearrange) return;
  bool was_up = s_swap_timer != NULL;
  int before = shown_rim();
  int next = -1;
  bool up;
  if (s_cfg.flick_mode == FLICK_ONE) {
    up = !was_up;                              // each flick toggles
    if (up && s_cfg.flick_target != s_cfg.rim) next = s_cfg.flick_target;
  } else {
    int cur = s_showing >= 0 ? s_showing : s_cfg.rim;
    for (int k = 1; k < RIM_COUNT; k++) {
      int m = (cur + k) % RIM_COUNT;
      if (m == s_cfg.rim) break;               // wrapped round to the default
      if (rim_enabled(m)) { next = m; break; }
    }
    // Up while a complication is showing; with none, the date or the
    // rearranged engines alone toggle.
    up = next >= 0 || (!was_up && (s_cfg.show_date || s_cfg.flick_rearrange));
  }
  s_showing = up ? next : -1;
  bool swap = up && s_cfg.flick_rearrange;
  if (swap != s_order_swapped && s_anim_step < 0) {
    s_order_swapped = swap;
    engines_transition();                      // flame out, then flare into place
    update_pulse();
  }
  if (shown_rim() != before) fx_start(FX_RIM, RIM_SWEEP_MS);
  if (up && s_cfg.show_date) readout_open();
  else readout_close();
  if (maracas_time()) fx_start(FX_SHAKE, SHAKE_MS);
  if (!up) {
    if (s_swap_timer) { app_timer_cancel(s_swap_timer); s_swap_timer = NULL; }
  } else if (s_swap_timer) {
    app_timer_reschedule(s_swap_timer, swap_ms());
  } else {
    s_swap_timer = app_timer_register(swap_ms(), swap_timeout, NULL);
  }
  update_tick_subscription();
  layer_mark_dirty(s_canvas);
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  if (s_cfg.flick_swap) flick_action();
}

// The accelerometer tap service only runs while flicks are wanted.
static bool s_accel_on = false;
static void update_accel_subscription(void) {
  if (s_cfg.flick_swap && !s_accel_on) {
    accel_tap_service_subscribe(accel_tap_handler);
    s_accel_on = true;
  } else if (!s_cfg.flick_swap && s_accel_on) {
    accel_tap_service_unsubscribe();
    s_accel_on = false;
  }
}

// --- tap on the touchscreen (Round 2): a short touch that barely moves.
#if defined(PBL_TOUCH)

#define TAP_MAX_MS    400
#define TAP_MAX_MOVE  14    // pixels

typedef struct {
  bool down, moved;
  int16_t x0, y0;
  uint64_t t0;
} TapTracker;

// Feed touch events in; returns true when one completes a tap.
static bool tap_tracker_feed(TapTracker *tr, TouchEventType type, int16_t x, int16_t y,
                             uint64_t now_ms) {
  switch (type) {
    case TouchEvent_Touchdown:
      tr->down = true; tr->moved = false;
      tr->x0 = x; tr->y0 = y; tr->t0 = now_ms;
      return false;
    case TouchEvent_PositionUpdate:
      if (tr->down && (abs(x - tr->x0) > TAP_MAX_MOVE || abs(y - tr->y0) > TAP_MAX_MOVE)) {
        tr->moved = true;
      }
      return false;
    case TouchEvent_Liftoff: {
      bool tap = tr->down && !tr->moved && now_ms - tr->t0 <= TAP_MAX_MS;
      tr->down = false;
      return tap;
    }
    default:
      return false;
  }
}

static TapTracker s_tap;
static bool s_touch_on = false;

static void touch_handler(const TouchEvent *event, void *context) {
  // Taps count even when the firmware marks them non-navigational (an idle
  // watchface), since the face is idle nearly all the time.
  if (tap_tracker_feed(&s_tap, event->type, event->x, event->y, now_ms())) flick_action();
}

// The touch sensor is powered while subscribed, so only subscribe when wanted.
static void update_touch_subscription(void) {
  bool want = s_cfg.tap_screen;
  if (want && !s_touch_on) {
    touch_service_subscribe(touch_handler, NULL);
    s_touch_on = true;
  } else if (!want && s_touch_on) {
    touch_service_unsubscribe();
    s_touch_on = false;
  }
}
#else
static void update_touch_subscription(void) {}   // no touchscreen on this watch
#endif

// ---------------------------------------------------------------- events

static void tick_handler(struct tm *t, TimeUnits changed) {
  s_now = *t;
  if (!(changed & MINUTE_UNIT)) {                // a seconds tick: just move the hand
    layer_mark_dirty(s_canvas);
    return;
  }
  s_td_mask_ok = false;                          // a burning readout re-reads its lettering
  // Ask the phone for fresh sun times (daily) and distances (every 3 hours).
  int need = (s_sun.yday != t->tm_yday ? NEED_SUN : 0) |
             (time(NULL) - s_orb.updated > 3 * 60 * 60 ? NEED_ORBIT : 0);
  if (t->tm_min % 15 == 0) request_data(need);
  // Maracas: a quick double buzz as they appear at midnight on Mars day.
  if (t->tm_min == 0 && maracas_time()) {
    static const uint32_t segs[] = { 70, 60, 70 };
    vibes_enqueue_custom_pattern((VibePattern){ .durations = segs, .num_segments = 3 });
    fx_start(FX_SHAKE, SHAKE_MS);
  }
  if (s_anim_step < 0) {
    engines_transition();                        // flare what lit, flame out what went dark
    // Sweep the rim when its jump complications move on.
    int shown = shown_rim();
    if (t->tm_min == 0 && (shown == RIM_HOURS || (t->tm_hour == 0 && shown == RIM_MONTH))) {
      fx_start(FX_RIM, RIM_SWEEP_MS);
    }
  }
  update_pulse();
  layer_mark_dirty(s_canvas);
}

// Tick every second only while the Seconds complication is on screen.
static TimeUnits s_tick_units = 0;
static void update_tick_subscription(void) {
  TimeUnits want = (shown_rim() == RIM_SECONDS && s_focused) ? SECOND_UNIT : MINUTE_UNIT;
  if (want == s_tick_units) return;
  s_tick_units = want;
  time_t now = time(NULL);
  s_now = *localtime(&now);                      // seconds may be stale
  tick_timer_service_subscribe(want, tick_handler);
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void connection_handler(bool connected) {
  if (!connected && s_connected && s_cfg.vibe_bt) vibes_double_pulse();
  s_connected = connected;
  layer_mark_dirty(s_canvas);
}

static int32_t tuple_int(const Tuple *t) {
  switch (t->type) {
    case TUPLE_CSTRING:
      return atoi(t->value->cstring);
    case TUPLE_INT:
      return t->length == 1 ? t->value->int8 :
             t->length == 2 ? t->value->int16 : t->value->int32;
    case TUPLE_UINT:
      return t->length == 1 ? t->value->uint8 :
             t->length == 2 ? t->value->uint16 : (int32_t)t->value->uint32;
    default:
      return 0;
  }
}

static int16_t clamp_minute(int32_t m) {
  return (m >= 0 && m < DAY_MIN) ? (int16_t)m : NO_TIME;
}

static void inbox_received(DictionaryIterator *it, void *context) {
  Tuple *t;
  bool changed = false;
  if ((t = dict_find(it, MESSAGE_KEY_Flame))) {
    int f = tuple_int(t);
    s_cfg.flame = (f >= 0 && f < FLAME_COUNT) ? f : FLAME_ORANGE;
    changed = true;
  }
  int rim_before = shown_rim();
  if ((t = dict_find(it, MESSAGE_KEY_RimMode))) {
    int m = tuple_int(t);
    s_cfg.rim = (m >= 0 && m < RIM_COUNT) ? m : RIM_HOURS;
    changed = true;
  }
  if ((t = dict_find(it, MESSAGE_KEY_FlickMode))) {
    int fm = tuple_int(t);
    if (fm == FLICK_ARRANGE_OLD) s_cfg.flick_rearrange = true;   // old settings page value
    s_cfg.flick_mode = fm == FLICK_ONE ? FLICK_ONE : FLICK_CYCLE;
    changed = true;
  }
  if ((t = dict_find(it, MESSAGE_KEY_FlickTarget))) {
    int m = tuple_int(t);
    s_cfg.flick_target = (m >= 0 && m < RIM_COUNT) ? m : RIM_STEPS;
    changed = true;
  }
  if ((t = dict_find(it, MESSAGE_KEY_FlickSeconds))) {
    int fs = tuple_int(t);
    s_cfg.flick_secs = (fs == 5 || fs == 10) ? fs : 8;
    changed = true;
  }
  if ((t = dict_find(it, MESSAGE_KEY_FlickReadout))) {
    s_cfg.readout = tuple_int(t) == READOUT_THROTTLE ? READOUT_THROTTLE : READOUT_BAND;
    changed = true;
  }
  if ((t = dict_find(it, MESSAGE_KEY_EngineOrder))) {
    s_cfg.engine_order = tuple_int(t) == ORDER_STARTUP ? ORDER_STARTUP : ORDER_DIAL;
    changed = true;
  }
  if ((t = dict_find(it, MESSAGE_KEY_StepGoal))) {
    int32_t g = tuple_int(t);
    if (g > 0) s_cfg.step_goal = g;
    changed = true;
  }
#define READ_FLAG(KEY, FIELD) \
  if ((t = dict_find(it, MESSAGE_KEY_##KEY))) { s_cfg.FIELD = tuple_int(t) != 0; changed = true; }
  READ_FLAG(FlickSwap, flick_swap)
  READ_FLAG(VibeDisconnect, vibe_bt)
  READ_FLAG(ShowSteps, show_steps)
  READ_FLAG(ShowDay, show_day)
  READ_FLAG(ShowMonth, show_month)
  READ_FLAG(ShowTMinus, show_tminus)
  READ_FLAG(ShowMoon, show_moon)
  READ_FLAG(ShowOrbits, show_orbits)
  READ_FLAG(ShowSeconds, show_seconds)
  READ_FLAG(ShowDate, show_date)
  READ_FLAG(TapScreen, tap_screen)
  READ_FLAG(PulseMinutes, pulse_minutes)
  READ_FLAG(FlickRearrange, flick_rearrange)
  if ((t = dict_find(it, MESSAGE_KEY_TMinusDate))) {
    int32_t day = t->type == TUPLE_CSTRING ? parse_date(t->value->cstring) : NO_DATE;
    if (day != s_cfg.tminus_day) {               // a new target restarts the long count
      s_cfg.tminus_day = day;
      s_cfg.tminus_start = day == NO_DATE ? NO_DATE : today_day();
    }
    changed = true;
  }
  if (changed) {
    save_settings();
    update_touch_subscription();
    update_accel_subscription();
    if (s_anim_step < 0) engines_transition();   // order or pulse may have changed
    if (shown_rim() != rim_before) fx_start(FX_RIM, RIM_SWEEP_MS);
    update_pulse();
    update_tick_subscription();
  }

  Tuple *moon_km = dict_find(it, MESSAGE_KEY_MoonDistKm);
  Tuple *mars_mau = dict_find(it, MESSAGE_KEY_MarsDistMilliAU);
  if (moon_km || mars_mau) {
    if (moon_km) s_orb.moon_km = tuple_int(moon_km);
    if (mars_mau) s_orb.mars_mau = tuple_int(mars_mau);
    Tuple *in;
    if ((in = dict_find(it, MESSAGE_KEY_MoonApproach))) s_orb.moon_in = tuple_int(in) != 0;
    if ((in = dict_find(it, MESSAGE_KEY_MarsApproach))) s_orb.mars_in = tuple_int(in) != 0;
    if ((in = dict_find(it, MESSAGE_KEY_MarsClosestDay))) s_orb.mars_closest_day = tuple_int(in);
    s_orb.updated = time(NULL);
    persist_write_data(ORBIT_KEY, &s_orb, sizeof(s_orb));
  }

  Tuple *rise = dict_find(it, MESSAGE_KEY_Sunrise);
  Tuple *set = dict_find(it, MESSAGE_KEY_Sunset);
  Tuple *noon = dict_find(it, MESSAGE_KEY_SolarNoon);
  if (rise || set || noon) {
    s_sun.rise = rise ? clamp_minute(tuple_int(rise)) : NO_TIME;
    s_sun.set = set ? clamp_minute(tuple_int(set)) : NO_TIME;
    s_sun.noon = noon ? clamp_minute(tuple_int(noon)) : NO_TIME;
    s_sun.yday = s_now.tm_yday;
    Tuple *south = dict_find(it, MESSAGE_KEY_SouthHemi);
    if (south) s_sun.south = tuple_int(south) != 0;
    persist_write_data(SUN_KEY, &s_sun, sizeof(s_sun));
  }
  layer_mark_dirty(s_canvas);
}

// ---------------------------------------------------------------- lifecycle

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
  s_pulse_layer = layer_create(GRect(0, 0, 1, 1));
  layer_set_update_proc(s_pulse_layer, pulse_draw);
  layer_set_hidden(s_pulse_layer, true);
  layer_add_child(root, s_pulse_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_pulse_layer);
  s_pulse_layer = NULL;
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

static void init(void) {
  load_state();
  time_t now = time(NULL);
  s_now = *localtime(&now);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 64);

  update_tick_subscription();
  s_connected = connection_service_peek_pebble_app_connection();
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = connection_handler,
  });
  update_accel_subscription();
  app_focus_service_subscribe(focus_handler);
  update_touch_subscription();
  update_steps();
#if defined(PBL_HEALTH)
  health_service_events_subscribe(health_handler, NULL);
#endif

  build_startup_sequence();
  s_anim_step = 0;
  s_anim = app_timer_register(350, anim_step, NULL);
}

static void deinit(void) {
  if (s_anim) app_timer_cancel(s_anim);
  if (s_swap_timer) app_timer_cancel(s_swap_timer);
  if (s_fx_timer) app_timer_cancel(s_fx_timer);
  tick_timer_service_unsubscribe();
  connection_service_unsubscribe();
  if (s_accel_on) accel_tap_service_unsubscribe();
  app_focus_service_unsubscribe();
  if (s_pulse_timer) app_timer_cancel(s_pulse_timer);
#if defined(PBL_TOUCH)
  if (s_touch_on) touch_service_unsubscribe();
#endif
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
