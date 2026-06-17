#include <pebble.h>

// ---------------------------------------------------------------------------
// Enumerations -- values must match those in src/pkjs/config.js
// ---------------------------------------------------------------------------

typedef enum {
  SLOT_NONE      = 0,
  SLOT_WEATHER   = 1,
  SLOT_DATE      = 2,
  SLOT_HEARTRATE = 3,
  SLOT_STEPS     = 4,
  SLOT_BATTERY   = 5,
} SlotType;

typedef enum {
  HAND_SOLID    = 0,  // filled rectangle + outline (original style)
  HAND_OUTLINE  = 1,  // outline only, transparent interior
  HAND_SKELETON = 2,  // outline + single center spine line
} HandStyle;

// ---------------------------------------------------------------------------
// Persist keys (must not collide)
// ---------------------------------------------------------------------------

#define PERSIST_LEFT_SLOT      0
#define PERSIST_RIGHT_SLOT     1
#define PERSIST_BOTTOM_SLOT    2
#define PERSIST_HAND_STYLE     3
#define PERSIST_SECONDS        4
#define PERSIST_VIBRATION      5
#define PERSIST_INVERTED       6
#define PERSIST_WEATHER_TEMP   7
#define PERSIST_WEATHER_COND   8
#define PERSIST_HAS_WEATHER    9

// ---------------------------------------------------------------------------
// Startup animation state machine
// ---------------------------------------------------------------------------

#define ANIM_IDLE    0
#define ANIM_START   1
#define ANIM_HOURS   2
#define ANIM_MINUTES 3
#define ANIM_SECONDS 4
#define ANIM_DONE    5

// ---------------------------------------------------------------------------
// Runtime settings (defaults shown; overridden by persist on startup)
// ---------------------------------------------------------------------------

static SlotType  s_left_slot       = SLOT_NONE;
static SlotType  s_right_slot      = SLOT_DATE;
static SlotType  s_bottom_slot     = SLOT_WEATHER;
static HandStyle s_hand_style      = HAND_SOLID;
static bool      s_display_seconds = false;
static bool      s_hour_vibration  = false;
static bool      s_inverted        = false;

static int       s_temperature     = 0;
static int       s_weather_cond    = 0;
static bool      s_has_weather     = false;

// ---------------------------------------------------------------------------
// Layer / window globals
// ---------------------------------------------------------------------------

static Window    *s_window;

static Layer     *s_bg_layer;
static Layer     *s_hour_layer;
static Layer     *s_minute_layer;
static Layer     *s_second_layer;
static Layer     *s_center_layer;

static TextLayer *s_left_text;
static TextLayer *s_right_text;
static TextLayer *s_bottom_text;

static char s_left_buf[32];
static char s_right_buf[32];
static char s_bottom_buf[32];

static GFont s_num_font;    // DigitalDream: hour numbers on the dial face
static GFont s_slot_font;   // DigitalDream: info slot text

static GPath *s_hour_path;
static GPath *s_minute_path;

// ---------------------------------------------------------------------------
// Animation state
// ---------------------------------------------------------------------------

static int       s_anim_state        = ANIM_IDLE;  // starts idle → triggers sweep on first tick
static uint32_t  s_hour_angle_anim   = 0;
static uint32_t  s_minute_angle_anim = 0;
static int32_t   s_second_angle_anim = 0;
static AppTimer *s_anim_timer        = NULL;

// ---------------------------------------------------------------------------
// Emery-scaled constants (compiled-in per platform via PBL_IF_EMERY_ELSE)
// ---------------------------------------------------------------------------

static const GPathInfo MINUTE_HAND_INFO = {
  .num_points = 4,
  .points = (GPoint []) {
    { PBL_IF_EMERY_ELSE(-6, -4), PBL_IF_EMERY_ELSE( 21,  15) },
    {  PBL_IF_EMERY_ELSE(6,  4), PBL_IF_EMERY_ELSE( 21,  15) },
    {  PBL_IF_EMERY_ELSE(6,  4), PBL_IF_EMERY_ELSE(-97, -70) },
    { PBL_IF_EMERY_ELSE(-6, -4), PBL_IF_EMERY_ELSE(-97, -70) },
  }
};

static const GPathInfo HOUR_HAND_INFO = {
  .num_points = 4,
  .points = (GPoint []) {
    { PBL_IF_EMERY_ELSE(-6, -4), PBL_IF_EMERY_ELSE( 21,  15) },
    {  PBL_IF_EMERY_ELSE(6,  4), PBL_IF_EMERY_ELSE( 21,  15) },
    {  PBL_IF_EMERY_ELSE(6,  4), PBL_IF_EMERY_ELSE(-69, -50) },
    { PBL_IF_EMERY_ELSE(-6, -4), PBL_IF_EMERY_ELSE(-69, -50) },
  }
};

#define SECOND_HAND_LEN  PBL_IF_EMERY_ELSE(97, 70)
#define CENTER_OUTER_R   PBL_IF_EMERY_ELSE(6,  4)
#define CENTER_INNER_R   PBL_IF_EMERY_ELSE(5,  3)
#define TICK_OUTER_INSET PBL_IF_EMERY_ELSE(5,  4)
#define TICK_HOUR_LEN    PBL_IF_EMERY_ELSE(12, 9)
#define TICK_MIN_LEN     PBL_IF_EMERY_ELSE(6,  4)
#define NUM_RADIUS_INSET PBL_IF_EMERY_ELSE(26, 18)
#define SLOT_MARGIN      PBL_IF_EMERY_ELSE(10, 6)
#define LOGO_Y_OFFSET    PBL_IF_EMERY_ELSE(18, 12)

// ---------------------------------------------------------------------------
// Weather helpers
// ---------------------------------------------------------------------------

static const char *condition_label(int cond) {
  switch (cond) {
    case 0:  return "clear";
    case 1:  return "p.cloudy";
    case 2:  return "cloudy";
    case 3:  return "rain";
    case 4:  return "snow";
    case 5:  return "thunder";
    default: return "";
  }
}

// ---------------------------------------------------------------------------
// Slot text
// ---------------------------------------------------------------------------

static void update_slot_buf(SlotType type, char *buf, size_t buf_size,
                             struct tm *tick_time) {
  switch (type) {
    case SLOT_NONE:
      buf[0] = '\0';
      break;

    case SLOT_DATE:
      if (tick_time) {
        strftime(buf, buf_size, "%a %d", tick_time);
      }
      break;

    case SLOT_WEATHER:
      if (!s_has_weather) {
        snprintf(buf, buf_size, "--");
      } else {
        snprintf(buf, buf_size, "%dc\n%s", s_temperature,
                 condition_label(s_weather_cond));
      }
      break;

    case SLOT_BATTERY: {
      BatteryChargeState bcs = battery_state_service_peek();
      snprintf(buf, buf_size, "%d%%", (int)bcs.charge_percent);
      break;
    }

#if PBL_HEALTH
    case SLOT_HEARTRATE: {
      HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
      if ((int)hr > 0) {
        snprintf(buf, buf_size, "%d bpm", (int)hr);
      } else {
        snprintf(buf, buf_size, "-- bpm");
      }
      break;
    }
    case SLOT_STEPS: {
      HealthValue steps = health_service_sum_today(HealthMetricStepCount);
      snprintf(buf, buf_size, "%d", (int)steps);
      break;
    }
#else
    case SLOT_HEARTRATE:
    case SLOT_STEPS:
      snprintf(buf, buf_size, "N/A");
      break;
#endif

    default:
      buf[0] = '\0';
      break;
  }
}

static void refresh_all_slots(struct tm *tick_time) {
  update_slot_buf(s_left_slot,   s_left_buf,   sizeof(s_left_buf),   tick_time);
  update_slot_buf(s_right_slot,  s_right_buf,  sizeof(s_right_buf),  tick_time);
  update_slot_buf(s_bottom_slot, s_bottom_buf, sizeof(s_bottom_buf), tick_time);

  text_layer_set_text(s_left_text,   s_left_buf);
  text_layer_set_text(s_right_text,  s_right_buf);
  text_layer_set_text(s_bottom_text, s_bottom_buf);
}

// ---------------------------------------------------------------------------
// Slot layer positioning
// ---------------------------------------------------------------------------

static void position_slot_layers(void) {
  GRect bounds = layer_get_bounds(window_get_root_layer(s_window));
  int16_t w  = bounds.size.w;
  int16_t h  = bounds.size.h;
  int16_t cx = w / 2;
  int16_t cy = h / 2;

  int slot_w = PBL_IF_EMERY_ELSE(70, 52);
  int slot_h = PBL_IF_EMERY_ELSE(40, 30);

  // Left slot: flush to left edge, vertically centered
  layer_set_frame(text_layer_get_layer(s_left_text),
      GRect(SLOT_MARGIN, cy - slot_h / 2, slot_w, slot_h));

  // Right slot: flush to right edge, vertically centered
  layer_set_frame(text_layer_get_layer(s_right_text),
      GRect(w - SLOT_MARGIN - slot_w, cy - slot_h / 2, slot_w, slot_h));

  // Bottom slot: horizontally centered, near bottom edge
  layer_set_frame(text_layer_get_layer(s_bottom_text),
      GRect(cx - slot_w / 2, h - SLOT_MARGIN - slot_h, slot_w, slot_h));

  layer_set_hidden(text_layer_get_layer(s_left_text),   s_left_slot   == SLOT_NONE);
  layer_set_hidden(text_layer_get_layer(s_right_text),  s_right_slot  == SLOT_NONE);
  layer_set_hidden(text_layer_get_layer(s_bottom_text), s_bottom_slot == SLOT_NONE);
}

// ---------------------------------------------------------------------------
// Background layer: dial face, numbers, logo, battery bar
// ---------------------------------------------------------------------------

static void bg_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int16_t w  = bounds.size.w;
  int16_t h  = bounds.size.h;
  int16_t cx = w / 2;
  int16_t cy = h / 2;

  int dial_r = (w < h ? w : h) / 2 - TICK_OUTER_INSET;

  GColor bg_color = s_inverted ? GColorWhite : GColorBlack;
  GColor fg_color = s_inverted ? GColorBlack : GColorWhite;

  graphics_context_set_fill_color(ctx, bg_color);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  graphics_context_set_stroke_color(ctx, fg_color);
  graphics_context_set_text_color(ctx, fg_color);

  // Tick marks (60 positions)
  for (int i = 0; i < 60; i++) {
    int32_t angle  = (TRIG_MAX_ANGLE * i) / 60;
    int     sin_v  = sin_lookup(angle);
    int     cos_v  = cos_lookup(angle);
    bool    is_hr  = (i % 5 == 0);
    int     t_len  = is_hr ? TICK_HOUR_LEN : TICK_MIN_LEN;
    int     outer  = dial_r;
    int     inner  = dial_r - t_len;

    GPoint op = GPoint(cx + outer * sin_v / TRIG_MAX_RATIO,
                       cy - outer * cos_v / TRIG_MAX_RATIO);
    GPoint ip = GPoint(cx + inner * sin_v / TRIG_MAX_RATIO,
                       cy - inner * cos_v / TRIG_MAX_RATIO);

    graphics_context_set_stroke_width(ctx, is_hr ? 2 : 1);
    graphics_draw_line(ctx, op, ip);
  }

  // Hour numerals 1-12
  int num_r   = dial_r - NUM_RADIUS_INSET;
  int font_sz = PBL_IF_EMERY_ELSE(16, 12);
  int cell_w  = font_sz + 6;
  int cell_h  = font_sz + 4;
  char num_buf[3];

  for (int n = 1; n <= 12; n++) {
    int32_t angle = (TRIG_MAX_ANGLE * n * 5) / 60;
    int     sin_v = sin_lookup(angle);
    int     cos_v = cos_lookup(angle);

    GPoint pos = GPoint(cx + num_r * sin_v / TRIG_MAX_RATIO - cell_w / 2,
                        cy - num_r * cos_v / TRIG_MAX_RATIO - cell_h / 2);

    snprintf(num_buf, sizeof(num_buf), "%d", n);
    graphics_draw_text(ctx, num_buf, s_num_font,
                       GRect(pos.x, pos.y, cell_w + 4, cell_h + 2),
                       GTextOverflowModeWordWrap,
                       GTextAlignmentCenter, NULL);
  }

  // "pebble" logo at top-center
  int logo_y  = LOGO_Y_OFFSET;
  int logo_w  = PBL_IF_EMERY_ELSE(64, 48);
  GFont logo_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  graphics_draw_text(ctx, "pebble", logo_font,
                     GRect(cx - logo_w / 2, logo_y, logo_w, 18),
                     GTextOverflowModeWordWrap,
                     GTextAlignmentCenter, NULL);

  // Battery bar directly below logo
  BatteryChargeState bcs = battery_state_service_peek();
  int bar_w    = PBL_IF_EMERY_ELSE(40, 28);
  int bar_h    = PBL_IF_EMERY_ELSE(5, 4);
  int bar_x    = cx - bar_w / 2;
  int bar_y    = logo_y + 20;
  int fill_w   = bar_w * bcs.charge_percent / 100;

  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_stroke_color(ctx, fg_color);
  graphics_draw_rect(ctx, GRect(bar_x, bar_y, bar_w, bar_h));
  if (fill_w > 0) {
    graphics_context_set_fill_color(ctx, fg_color);
    graphics_fill_rect(ctx, GRect(bar_x, bar_y, fill_w, bar_h), 0, GCornerNone);
  }
}

// ---------------------------------------------------------------------------
// Hand drawing
// ---------------------------------------------------------------------------

// Draw a rotated rectangular hand path in the chosen style.
// `path` must already be rotated via gpath_rotate_to().
static void draw_hand_path(GContext *ctx, GPath *path) {
  GColor hand_fill    = s_inverted ? GColorBlack : GColorWhite;
  GColor hand_stroke  = s_inverted ? GColorWhite : GColorBlack;

  switch (s_hand_style) {
    case HAND_SOLID:
      graphics_context_set_fill_color(ctx, hand_fill);
      graphics_context_set_stroke_color(ctx, hand_stroke);
      gpath_draw_filled(ctx, path);
      gpath_draw_outline(ctx, path);
      break;

    case HAND_OUTLINE:
      graphics_context_set_stroke_color(ctx, hand_stroke);
      graphics_context_set_stroke_width(ctx, 1);
      gpath_draw_outline(ctx, path);
      break;

    case HAND_SKELETON: {
      graphics_context_set_stroke_color(ctx, hand_stroke);
      graphics_context_set_stroke_width(ctx, 1);
      gpath_draw_outline(ctx, path);

      // Compute the spine line from tail midpoint to tip midpoint.
      // path->points holds the pre-rotation vertices; rotation and translation
      // are stored in path->rotation and path->offset respectively.
      //
      // The hand rectangle has:
      //   pts[0].y = pts[1].y = tail_y  (positive, below center)
      //   pts[2].y = pts[3].y = tip_y   (negative, above center)
      //
      // For a point (0, py) rotated clockwise by `rot` around origin then
      // translated by `offset`:
      //   world_x = offset.x - py * sin(rot) / TRIG_MAX_RATIO
      //   world_y = offset.y + py * cos(rot) / TRIG_MAX_RATIO
      //
      int32_t rot     = path->rotation;
      GPoint  offset  = path->offset;
      int16_t tail_y  = path->points[0].y;  // e.g. 15 or 21
      int16_t tip_y   = path->points[2].y;  // e.g. -70 or -50

      GPoint tail = GPoint(
          offset.x - tail_y * (int)sin_lookup(rot) / TRIG_MAX_RATIO,
          offset.y + tail_y * (int)cos_lookup(rot) / TRIG_MAX_RATIO);
      GPoint tip = GPoint(
          offset.x - tip_y * (int)sin_lookup(rot) / TRIG_MAX_RATIO,
          offset.y + tip_y * (int)cos_lookup(rot) / TRIG_MAX_RATIO);

      graphics_draw_line(ctx, tail, tip);
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// Hour hand
// ---------------------------------------------------------------------------

static void hour_update_proc(Layer *layer, GContext *ctx) {
  time_t      now = time(NULL);
  struct tm  *t   = localtime(&now);
  unsigned int angle = (unsigned)(t->tm_hour % 12) * 30 + t->tm_min / 2;

  if (s_anim_state < ANIM_HOURS) {
    angle = 0;
  } else if (s_anim_state == ANIM_HOURS) {
    if (s_hour_angle_anim == 0 && t->tm_hour >= 12) {
      s_hour_angle_anim = 360;
    }
    s_hour_angle_anim += 6;
    if (s_hour_angle_anim >= angle) {
      s_anim_state = ANIM_MINUTES;
    } else {
      angle = s_hour_angle_anim;
    }
  }

  gpath_rotate_to(s_hour_path, (TRIG_MAX_ANGLE / 360) * angle);
  draw_hand_path(ctx, s_hour_path);
}

// ---------------------------------------------------------------------------
// Minute hand
// ---------------------------------------------------------------------------

static void minute_update_proc(Layer *layer, GContext *ctx) {
  time_t      now = time(NULL);
  struct tm  *t   = localtime(&now);
  unsigned int angle = (unsigned)t->tm_min * 6 + (unsigned)t->tm_sec / 10;

  if (s_anim_state < ANIM_MINUTES) {
    angle = 0;
  } else if (s_anim_state == ANIM_MINUTES) {
    s_minute_angle_anim += 6;
    if (s_minute_angle_anim >= angle) {
      s_anim_state = s_display_seconds ? ANIM_SECONDS : ANIM_DONE;
    } else {
      angle = s_minute_angle_anim;
    }
  }

  gpath_rotate_to(s_minute_path, (TRIG_MAX_ANGLE / 360) * angle);
  draw_hand_path(ctx, s_minute_path);
}

// ---------------------------------------------------------------------------
// Second hand
// ---------------------------------------------------------------------------

static void second_update_proc(Layer *layer, GContext *ctx) {
  GRect  bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);

  time_t     now          = time(NULL);
  struct tm *t            = localtime(&now);
  int32_t    second_angle = (int32_t)t->tm_sec * (TRIG_MAX_ANGLE / 60);
  GPoint tip;

  if (s_anim_state < ANIM_SECONDS) {
    tip = GPoint(center.x, center.y - SECOND_HAND_LEN);
  } else if (s_anim_state == ANIM_SECONDS) {
    s_second_angle_anim += TRIG_MAX_ANGLE / 60;
    if (s_second_angle_anim >= second_angle) {
      s_anim_state = ANIM_DONE;
    }
    int32_t draw_angle = (s_anim_state == ANIM_DONE) ? second_angle : s_second_angle_anim;
    tip = GPoint(
        center.x + SECOND_HAND_LEN * sin_lookup(draw_angle) / TRIG_MAX_RATIO,
        center.y - SECOND_HAND_LEN * cos_lookup(draw_angle) / TRIG_MAX_RATIO);
  } else {
    tip = GPoint(
        center.x + SECOND_HAND_LEN * sin_lookup(second_angle) / TRIG_MAX_RATIO,
        center.y - SECOND_HAND_LEN * cos_lookup(second_angle) / TRIG_MAX_RATIO);
  }

  GColor fg = s_inverted ? GColorBlack : GColorWhite;
  graphics_context_set_stroke_color(ctx, fg);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, center, tip);
}

// ---------------------------------------------------------------------------
// Center dot
// ---------------------------------------------------------------------------

static void center_update_proc(Layer *layer, GContext *ctx) {
  GRect  bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);

  GColor outer = s_inverted ? GColorBlack : GColorWhite;
  GColor inner = s_inverted ? GColorWhite : GColorBlack;

  graphics_context_set_fill_color(ctx, outer);
  graphics_fill_circle(ctx, center, CENTER_OUTER_R);
  graphics_context_set_fill_color(ctx, inner);
  graphics_fill_circle(ctx, center, CENTER_INNER_R);
}

// ---------------------------------------------------------------------------
// Animation timer
// ---------------------------------------------------------------------------

static void anim_timer_callback(void *data) {
  s_anim_timer = NULL;
  switch (s_anim_state) {
    case ANIM_START:
      s_anim_state = ANIM_HOURS;
      s_anim_timer = app_timer_register(50, anim_timer_callback, NULL);
      break;
    case ANIM_HOURS:
      layer_mark_dirty(s_hour_layer);
      s_anim_timer = app_timer_register(50, anim_timer_callback, NULL);
      break;
    case ANIM_MINUTES:
      layer_mark_dirty(s_minute_layer);
      s_anim_timer = app_timer_register(50, anim_timer_callback, NULL);
      break;
    case ANIM_SECONDS:
      if (s_display_seconds) {
        layer_mark_dirty(s_second_layer);
        s_anim_timer = app_timer_register(50, anim_timer_callback, NULL);
      } else {
        s_anim_state = ANIM_DONE;
      }
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Tick handler
// ---------------------------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // First tick ever → start the sweep animation
  if (s_anim_state == ANIM_IDLE) {
    s_anim_state = ANIM_START;
    s_anim_timer = app_timer_register(50, anim_timer_callback, NULL);
    return;
  }

  // Let animation complete before normal processing
  if (s_anim_state != ANIM_DONE) {
    return;
  }

  bool is_new_minute;
  if (s_display_seconds) {
    // We're subscribed to SECOND_UNIT; minute boundary is when sec wraps
    if (units_changed & SECOND_UNIT) {
      layer_mark_dirty(s_second_layer);
      if (tick_time->tm_sec % 10 == 0) {
        layer_mark_dirty(s_minute_layer);
      }
    }
    is_new_minute = (tick_time->tm_sec == 0);
  } else {
    // Subscribed to MINUTE_UNIT
    layer_mark_dirty(s_minute_layer);
    is_new_minute = true;
  }

  if (is_new_minute) {
    if (tick_time->tm_min % 2 == 0) {
      layer_mark_dirty(s_hour_layer);
    }
    if (s_hour_vibration && tick_time->tm_min == 0 &&
        tick_time->tm_hour >= 8 && tick_time->tm_hour <= 22) {
      vibes_double_pulse();
    }
    if (tick_time->tm_min == 0 && tick_time->tm_hour == 0) {
      // Midnight: date rolls over
      refresh_all_slots(tick_time);
      return;
    }
  }

  refresh_all_slots(tick_time);
}

// ---------------------------------------------------------------------------
// Battery state handler
// ---------------------------------------------------------------------------

static void battery_handler(BatteryChargeState state) {
  layer_mark_dirty(s_bg_layer);
  if (s_left_slot == SLOT_BATTERY || s_right_slot == SLOT_BATTERY ||
      s_bottom_slot == SLOT_BATTERY) {
    time_t now = time(NULL);
    refresh_all_slots(localtime(&now));
  }
}

// ---------------------------------------------------------------------------
// Health service handler
// ---------------------------------------------------------------------------

#if PBL_HEALTH
static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventHeartRateUpdate) {
    if (s_left_slot == SLOT_HEARTRATE || s_right_slot == SLOT_HEARTRATE ||
        s_bottom_slot == SLOT_HEARTRATE) {
      time_t now = time(NULL);
      refresh_all_slots(localtime(&now));
    }
  }
}
#endif

// ---------------------------------------------------------------------------
// AppMessage: Clay settings + weather data
// ---------------------------------------------------------------------------

static void apply_color_scheme(void) {
  GColor text_col = s_inverted ? GColorBlack : GColorWhite;
  text_layer_set_text_color(s_left_text,   text_col);
  text_layer_set_text_color(s_right_text,  text_col);
  text_layer_set_text_color(s_bottom_text, text_col);
  text_layer_set_background_color(s_left_text,   GColorClear);
  text_layer_set_background_color(s_right_text,  GColorClear);
  text_layer_set_background_color(s_bottom_text, GColorClear);
}

static void resubscribe_tick(void) {
  tick_timer_service_unsubscribe();
  tick_timer_service_subscribe(
      s_display_seconds ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  bool layout_changed  = false;
  bool style_changed   = false;
  bool weather_changed = false;
  Tuple *t;

  if ((t = dict_find(iter, MESSAGE_KEY_leftSlot))) {
    SlotType v = (SlotType)t->value->int32;
    if (v != s_left_slot) { s_left_slot = v; layout_changed = true; }
    persist_write_int(PERSIST_LEFT_SLOT, (int)v);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_rightSlot))) {
    SlotType v = (SlotType)t->value->int32;
    if (v != s_right_slot) { s_right_slot = v; layout_changed = true; }
    persist_write_int(PERSIST_RIGHT_SLOT, (int)v);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_bottomSlot))) {
    SlotType v = (SlotType)t->value->int32;
    if (v != s_bottom_slot) { s_bottom_slot = v; layout_changed = true; }
    persist_write_int(PERSIST_BOTTOM_SLOT, (int)v);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_handStyle))) {
    HandStyle v = (HandStyle)t->value->int32;
    if (v != s_hand_style) { s_hand_style = v; style_changed = true; }
    persist_write_int(PERSIST_HAND_STYLE, (int)v);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_displaySeconds))) {
    bool v = (bool)t->value->int32;
    if (v != s_display_seconds) { s_display_seconds = v; style_changed = true; }
    persist_write_bool(PERSIST_SECONDS, v);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_hourVibration))) {
    s_hour_vibration = (bool)t->value->int32;
    persist_write_bool(PERSIST_VIBRATION, s_hour_vibration);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_inverted))) {
    bool v = (bool)t->value->int32;
    if (v != s_inverted) { s_inverted = v; style_changed = true; }
    persist_write_bool(PERSIST_INVERTED, v);
  }

  // Weather data from index.js (not from Clay)
  if ((t = dict_find(iter, MESSAGE_KEY_weatherTemperature))) {
    s_temperature = (int)t->value->int32;
    s_has_weather = true;
    persist_write_int(PERSIST_WEATHER_TEMP, s_temperature);
    persist_write_bool(PERSIST_HAS_WEATHER, true);
    weather_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_weatherCondition))) {
    s_weather_cond = (int)t->value->int32;
    persist_write_int(PERSIST_WEATHER_COND, s_weather_cond);
    weather_changed = true;
  }

  if (layout_changed) {
    position_slot_layers();
  }
  if (style_changed) {
    layer_set_hidden(s_second_layer, !s_display_seconds);
    resubscribe_tick();
    apply_color_scheme();
  }
  if (layout_changed || style_changed || weather_changed) {
    time_t now = time(NULL);
    refresh_all_slots(localtime(&now));
    layer_mark_dirty(s_bg_layer);
    layer_mark_dirty(s_hour_layer);
    layer_mark_dirty(s_minute_layer);
    layer_mark_dirty(s_center_layer);
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

// ---------------------------------------------------------------------------
// Window load / unload
// ---------------------------------------------------------------------------

static void window_load(Window *window) {
  Layer *root  = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  // Load fonts (12px standard, 16px for Emery)
  s_num_font = fonts_load_custom_font(resource_get_handle(
      PBL_IF_EMERY_ELSE(RESOURCE_ID_FONT_DIGITALDREAM_NARROW_16,
                        RESOURCE_ID_FONT_DIGITALDREAM_NARROW_12)));
  s_slot_font = fonts_load_custom_font(resource_get_handle(
      PBL_IF_EMERY_ELSE(RESOURCE_ID_FONT_DIGITALDREAM_NARROW_16,
                        RESOURCE_ID_FONT_DIGITALDREAM_NARROW_12)));

  // Background dial layer
  s_bg_layer = layer_create(bounds);
  layer_set_update_proc(s_bg_layer, bg_update_proc);
  layer_add_child(root, s_bg_layer);

  // Info slot text layers (positioned by position_slot_layers below)
  s_left_text = text_layer_create(GRectZero);
  text_layer_set_font(s_left_text, s_slot_font);
  text_layer_set_overflow_mode(s_left_text, GTextOverflowModeWordWrap);
  text_layer_set_text_alignment(s_left_text, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_left_text));

  s_right_text = text_layer_create(GRectZero);
  text_layer_set_font(s_right_text, s_slot_font);
  text_layer_set_overflow_mode(s_right_text, GTextOverflowModeWordWrap);
  text_layer_set_text_alignment(s_right_text, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_right_text));

  s_bottom_text = text_layer_create(GRectZero);
  text_layer_set_font(s_bottom_text, s_slot_font);
  text_layer_set_overflow_mode(s_bottom_text, GTextOverflowModeWordWrap);
  text_layer_set_text_alignment(s_bottom_text, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_bottom_text));

  apply_color_scheme();
  position_slot_layers();

  // Hand layers (span full window; paths are centered via gpath_move_to)
  s_hour_layer = layer_create(bounds);
  layer_set_update_proc(s_hour_layer, hour_update_proc);
  layer_add_child(root, s_hour_layer);

  s_minute_layer = layer_create(bounds);
  layer_set_update_proc(s_minute_layer, minute_update_proc);
  layer_add_child(root, s_minute_layer);

  s_second_layer = layer_create(bounds);
  layer_set_update_proc(s_second_layer, second_update_proc);
  layer_set_hidden(s_second_layer, !s_display_seconds);
  layer_add_child(root, s_second_layer);

  s_center_layer = layer_create(bounds);
  layer_set_update_proc(s_center_layer, center_update_proc);
  layer_add_child(root, s_center_layer);

  // Build GPath objects and move their pivot to screen center
  s_hour_path   = gpath_create(&HOUR_HAND_INFO);
  s_minute_path = gpath_create(&MINUTE_HAND_INFO);
  GPoint center = grect_center_point(&bounds);
  gpath_move_to(s_hour_path,   center);
  gpath_move_to(s_minute_path, center);

  // Initial slot text content
  time_t now = time(NULL);
  refresh_all_slots(localtime(&now));
}

static void window_unload(Window *window) {
  gpath_destroy(s_hour_path);
  gpath_destroy(s_minute_path);

  layer_destroy(s_center_layer);
  layer_destroy(s_second_layer);
  layer_destroy(s_minute_layer);
  layer_destroy(s_hour_layer);

  text_layer_destroy(s_bottom_text);
  text_layer_destroy(s_right_text);
  text_layer_destroy(s_left_text);

  layer_destroy(s_bg_layer);

  fonts_unload_custom_font(s_slot_font);
  fonts_unload_custom_font(s_num_font);
}

// ---------------------------------------------------------------------------
// Persist helpers
// ---------------------------------------------------------------------------

static void load_persisted_settings(void) {
  if (persist_exists(PERSIST_LEFT_SLOT))
    s_left_slot = (SlotType)persist_read_int(PERSIST_LEFT_SLOT);
  if (persist_exists(PERSIST_RIGHT_SLOT))
    s_right_slot = (SlotType)persist_read_int(PERSIST_RIGHT_SLOT);
  if (persist_exists(PERSIST_BOTTOM_SLOT))
    s_bottom_slot = (SlotType)persist_read_int(PERSIST_BOTTOM_SLOT);
  if (persist_exists(PERSIST_HAND_STYLE))
    s_hand_style = (HandStyle)persist_read_int(PERSIST_HAND_STYLE);
  if (persist_exists(PERSIST_SECONDS))
    s_display_seconds = persist_read_bool(PERSIST_SECONDS);
  if (persist_exists(PERSIST_VIBRATION))
    s_hour_vibration = persist_read_bool(PERSIST_VIBRATION);
  if (persist_exists(PERSIST_INVERTED))
    s_inverted = persist_read_bool(PERSIST_INVERTED);
  if (persist_exists(PERSIST_WEATHER_TEMP))
    s_temperature = persist_read_int(PERSIST_WEATHER_TEMP);
  if (persist_exists(PERSIST_WEATHER_COND))
    s_weather_cond = persist_read_int(PERSIST_WEATHER_COND);
  if (persist_exists(PERSIST_HAS_WEATHER))
    s_has_weather = persist_read_bool(PERSIST_HAS_WEATHER);
}

// ---------------------------------------------------------------------------
// App init / deinit / main
// ---------------------------------------------------------------------------

static void init(void) {
  load_persisted_settings();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  resubscribe_tick();
  battery_state_service_subscribe(battery_handler);

#if PBL_HEALTH
  health_service_events_subscribe(health_handler, NULL);
#endif

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_open(256, 64);
}

static void deinit(void) {
  if (s_anim_timer) {
    app_timer_cancel(s_anim_timer);
    s_anim_timer = NULL;
  }
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
#if PBL_HEALTH
  health_service_events_unsubscribe();
#endif
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
