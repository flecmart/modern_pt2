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
  HAND_SOLID    = 0,
  HAND_OUTLINE  = 1,
  HAND_DIAMOND  = 2,
} HandStyle;

// ---------------------------------------------------------------------------
// Persist keys
// ---------------------------------------------------------------------------

#define PERSIST_LEFT_SLOT      0
#define PERSIST_RIGHT_SLOT     1
#define PERSIST_BOTTOM_SLOT    2
#define PERSIST_HAND_STYLE     3
#define PERSIST_SECONDS        4
#define PERSIST_VIBRATION      5
#define PERSIST_WEATHER_TEMP   6
#define PERSIST_WEATHER_COND   7
#define PERSIST_HAS_WEATHER    8
#define PERSIST_USE_FAHRENHEIT 9
#define PERSIST_ACCENT_COLOR   10

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
// Emery (200x228) constants
// ---------------------------------------------------------------------------

#define SECOND_HAND_LEN  97
#define CENTER_OUTER_R    6
#define CENTER_INNER_R    5

#define SLOT_W            48
#define SLOT_H            36
#define SLOT_LEFT_X       38
#define SLOT_RIGHT_X     (200 - 38 - SLOT_W)
#define SLOT_Y            (114 - SLOT_H / 2)
#define SLOT_BOTTOM_Y    146

#define BAT_BAR_W         51
#define BAT_BAR_X         75
#define BAT_BAR_Y         74

#define LOGO_X            73
#define LOGO_Y            59
#define LOGO_W            66
#define LOGO_H            17

// ---------------------------------------------------------------------------
// Runtime settings
// ---------------------------------------------------------------------------

static SlotType  s_left_slot       = SLOT_WEATHER;
static SlotType  s_right_slot      = SLOT_HEARTRATE;
static SlotType  s_bottom_slot     = SLOT_DATE;
static HandStyle s_hand_style      = HAND_DIAMOND;
static bool      s_display_seconds = false;
static bool      s_hour_vibration  = false;

static int       s_temperature     = 0;
static int       s_weather_cond    = 0;
static bool      s_has_weather     = false;
static bool      s_use_fahrenheit  = false;
static GColor    s_accent_color    = { .argb = 0xFF }; // GColorWhite default

// ---------------------------------------------------------------------------
// Layer / window globals
// ---------------------------------------------------------------------------

static Window      *s_window;

static BitmapLayer *s_bg_bitmap_layer;
static GBitmap     *s_bg_bitmap;

static GBitmap     *s_logo_bitmap;
static BitmapLayer *s_logo_layer;

static Layer       *s_battery_bar_layer;
static Layer       *s_hour_layer;
static Layer       *s_minute_layer;
static Layer       *s_second_layer;
static Layer       *s_center_layer;

static TextLayer   *s_left_text;
static TextLayer   *s_right_text;
static TextLayer   *s_bottom_text;

static char s_left_buf[32];
static char s_right_buf[32];
static char s_bottom_buf[32];

static GFont s_slot_font;

static GPath *s_hour_path;
static GPath *s_hour_diamond;
static GPath *s_minute_path;
static GPath *s_minute_diamond;

static GBitmap     *s_heart_bitmap;
static BitmapLayer *s_hr_icon_layer;

static GBitmap     *s_weather_bitmaps[6];
static BitmapLayer *s_weather_icon_layer;

static GBitmap     *s_battery_bitmap;
static BitmapLayer *s_bat_icon_layer;

static GBitmap     *s_steps_bitmap;
static BitmapLayer *s_steps_icon_layer;


// ---------------------------------------------------------------------------
// Animation state
// ---------------------------------------------------------------------------

static int       s_anim_state        = ANIM_IDLE;
static uint32_t  s_hour_angle_anim   = 0;
static uint32_t  s_minute_angle_anim = 0;
static int32_t   s_second_angle_anim = 0;
static AppTimer *s_anim_timer        = NULL;
static struct tm s_tick_time; // shared snapshot, updated once per tick

// ---------------------------------------------------------------------------
// Hand geometry for Emery (200x228)
// ---------------------------------------------------------------------------

static const GPathInfo MINUTE_HAND_INFO = {
  .num_points = 4,
  .points = (GPoint []) {
    { -6,  21 },
    {  6,  21 },
    {  6, -97 },
    { -6, -97 },
  }
};
static const GPathInfo MINUTE_DIAMOND_INFO = {
  .num_points = 4,
  .points = (GPoint []) {
    {  0,  -6 },
    {  4, -43 },
    {  0, -80 },
    { -4, -43 },
  }
};

static const GPathInfo HOUR_HAND_INFO = {
  .num_points = 4,
  .points = (GPoint []) {
    { -6,  21 },
    {  6,  21 },
    {  6, -69 },
    { -6, -69 },
  }
};
static const GPathInfo HOUR_DIAMOND_INFO = {
  .num_points = 4,
  .points = (GPoint []) {
    {  0,  -6 },
    {  4, -34 },
    {  0, -62 },
    { -4, -34 },
  }
};

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
        strftime(buf, buf_size, "%a\n%d", tick_time);
      }
      break;

    case SLOT_WEATHER:
      if (!s_has_weather) {
        snprintf(buf, buf_size, "\n--");
      } else {
        snprintf(buf, buf_size, "\n%d%s", s_temperature,
                 s_use_fahrenheit ? "F" : "C");
      }
      break;

    case SLOT_BATTERY: {
      BatteryChargeState bcs = battery_state_service_peek();
      snprintf(buf, buf_size, "\n%d%%", (int)bcs.charge_percent);
      break;
    }

    case SLOT_HEARTRATE: {
      HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
      if ((int)hr > 0) {
        snprintf(buf, buf_size, "\n%d", (int)hr);
      } else {
        snprintf(buf, buf_size, "\n--");
      }
      break;
    }

    case SLOT_STEPS: {
      HealthValue steps = health_service_sum_today(HealthMetricStepCount);
      snprintf(buf, buf_size, "\n%d", (int)steps);
      break;
    }

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
// Icon size constants
// ---------------------------------------------------------------------------

#define HEART_W   11
#define HEART_H   10
#define WICON_W   13
#define WICON_H   13
#define BATT_W    14
#define BATT_H     8
#define STEPS_W    9
#define STEPS_H   13

// ---------------------------------------------------------------------------
// Weather icon selection
// ---------------------------------------------------------------------------

static void update_weather_icon(void) {
  if (!s_weather_icon_layer) return;
  int idx = s_weather_cond;
  if (idx < 0 || idx > 5) idx = 0;
  bitmap_layer_set_bitmap(s_weather_icon_layer, s_weather_bitmaps[idx]);
}

// ---------------------------------------------------------------------------
// Slot layer positioning (tuned for Emery 200x228)
// ---------------------------------------------------------------------------

static void position_icon_in_slot(BitmapLayer *icon, int16_t slot_x, int16_t slot_y,
                                   int16_t icon_w, int16_t icon_h, bool visible) {
  if (!icon) return;
  int16_t ix = slot_x + (SLOT_W - icon_w) / 2;
  int16_t iy = slot_y + 1;
  layer_set_frame(bitmap_layer_get_layer(icon), GRect(ix, iy, icon_w, icon_h));
  layer_set_hidden(bitmap_layer_get_layer(icon), !visible);
}

static void find_slot_pos(SlotType target, int16_t lx, int16_t ly,
                           int16_t rx, int16_t ry, int16_t bx, int16_t by,
                           bool *vis, int16_t *sx, int16_t *sy) {
  *vis = false;
  if (s_left_slot == target)        { *vis = true; *sx = lx; *sy = ly; }
  else if (s_right_slot == target)  { *vis = true; *sx = rx; *sy = ry; }
  else if (s_bottom_slot == target) { *vis = true; *sx = bx; *sy = by; }
}

static void position_slot_layers(void) {
  GRect bounds = layer_get_bounds(window_get_root_layer(s_window));
  int16_t cx = bounds.size.w / 2;

  int16_t lx = SLOT_LEFT_X,  ly = SLOT_Y;
  int16_t rx = SLOT_RIGHT_X, ry = SLOT_Y;
  int16_t bx = cx - SLOT_W / 2, by = SLOT_BOTTOM_Y;

  layer_set_frame(text_layer_get_layer(s_left_text),   GRect(lx, ly, SLOT_W, SLOT_H));
  layer_set_frame(text_layer_get_layer(s_right_text),  GRect(rx, ry, SLOT_W, SLOT_H));
  layer_set_frame(text_layer_get_layer(s_bottom_text), GRect(bx, by, SLOT_W, SLOT_H));

  layer_set_hidden(text_layer_get_layer(s_left_text),   s_left_slot   == SLOT_NONE);
  layer_set_hidden(text_layer_get_layer(s_right_text),  s_right_slot  == SLOT_NONE);
  layer_set_hidden(text_layer_get_layer(s_bottom_text), s_bottom_slot == SLOT_NONE);

  bool vis = false; int16_t sx = 0, sy = 0;

  find_slot_pos(SLOT_HEARTRATE, lx, ly, rx, ry, bx, by, &vis, &sx, &sy);
  position_icon_in_slot(s_hr_icon_layer, sx, sy, HEART_W, HEART_H, vis);

  find_slot_pos(SLOT_WEATHER, lx, ly, rx, ry, bx, by, &vis, &sx, &sy);
  position_icon_in_slot(s_weather_icon_layer, sx, sy, WICON_W, WICON_H, vis);

  find_slot_pos(SLOT_BATTERY, lx, ly, rx, ry, bx, by, &vis, &sx, &sy);
  position_icon_in_slot(s_bat_icon_layer, sx, sy, BATT_W, BATT_H, vis);

  find_slot_pos(SLOT_STEPS, lx, ly, rx, ry, bx, by, &vis, &sx, &sy);
  position_icon_in_slot(s_steps_icon_layer, sx, sy, STEPS_W, STEPS_H, vis);

  update_weather_icon();
}

// ---------------------------------------------------------------------------
// Battery bar (drawn under "pebble" logo on the bitmap)
// ---------------------------------------------------------------------------

static void battery_bar_update_proc(Layer *layer, GContext *ctx) {
  BatteryChargeState bcs = battery_state_service_peek();
  int fill_w = BAT_BAR_W * bcs.charge_percent / 100;

  // Dim unfilled portion (layer origin = BAT_BAR_X, BAT_BAR_Y-1; draw at local y=1)
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx,
      GPoint(0, 1),
      GPoint(BAT_BAR_W - 1, 1));

  // Bright filled portion on top
  if (fill_w > 0) {
    graphics_context_set_stroke_color(ctx, s_accent_color);
    graphics_draw_line(ctx,
        GPoint(0, 1),
        GPoint(fill_w - 1, 1));
  }
}

// ---------------------------------------------------------------------------
// Hand drawing
// ---------------------------------------------------------------------------

static void draw_hand_path(GContext *ctx, GPath *path, GPath *diamond) {
  switch (s_hand_style) {
    case HAND_SOLID:
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_context_set_stroke_color(ctx, GColorBlack);
      gpath_draw_filled(ctx, path);
      gpath_draw_outline(ctx, path);
      break;

    case HAND_OUTLINE:
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_context_set_stroke_width(ctx, 3);
      gpath_draw_outline(ctx, path);
      break;

    case HAND_DIAMOND:
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_context_set_stroke_color(ctx, GColorBlack);
      gpath_draw_filled(ctx, path);
      gpath_draw_outline(ctx, path);
      graphics_context_set_fill_color(ctx, GColorBlack);
      gpath_draw_filled(ctx, diamond);
      break;
  }
}

// ---------------------------------------------------------------------------
// Hour hand
// ---------------------------------------------------------------------------

static void hour_update_proc(Layer *layer, GContext *ctx) {
  struct tm  *t   = &s_tick_time;
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

  int32_t pebble_angle = (TRIG_MAX_ANGLE / 360) * angle;
  gpath_rotate_to(s_hour_path,    pebble_angle);
  gpath_rotate_to(s_hour_diamond, pebble_angle);
  draw_hand_path(ctx, s_hour_path, s_hour_diamond);
}

// ---------------------------------------------------------------------------
// Minute hand
// ---------------------------------------------------------------------------

static void minute_update_proc(Layer *layer, GContext *ctx) {
  struct tm  *t   = &s_tick_time;
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

  int32_t pebble_angle = (TRIG_MAX_ANGLE / 360) * angle;
  gpath_rotate_to(s_minute_path,    pebble_angle);
  gpath_rotate_to(s_minute_diamond, pebble_angle);
  draw_hand_path(ctx, s_minute_path, s_minute_diamond);
}

// ---------------------------------------------------------------------------
// Second hand
// ---------------------------------------------------------------------------

static void second_update_proc(Layer *layer, GContext *ctx) {
  GRect  bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);

  struct tm *t            = &s_tick_time;
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

  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, center, tip);
}

// ---------------------------------------------------------------------------
// Center dot
// ---------------------------------------------------------------------------

static void center_update_proc(Layer *layer, GContext *ctx) {
  GRect  bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, CENTER_OUTER_R);
  graphics_context_set_fill_color(ctx, GColorWhite);
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
  if (s_anim_state == ANIM_IDLE) {
    s_anim_state = ANIM_START;
    s_anim_timer = app_timer_register(50, anim_timer_callback, NULL);
    return;
  }

  if (s_anim_state != ANIM_DONE) {
    return;
  }

  s_tick_time = *tick_time;

  bool is_new_minute;
  if (s_display_seconds) {
    if (units_changed & SECOND_UNIT) {
      layer_mark_dirty(s_second_layer);
      if (tick_time->tm_sec % 10 == 0) {
        layer_mark_dirty(s_minute_layer);
      }
    }
    is_new_minute = (tick_time->tm_sec == 0);
  } else {
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
    refresh_all_slots(tick_time);
  }
}

// ---------------------------------------------------------------------------
// Battery state handler
// ---------------------------------------------------------------------------

static void battery_handler(BatteryChargeState state) {
  layer_mark_dirty(s_battery_bar_layer);
  if (s_left_slot == SLOT_BATTERY || s_right_slot == SLOT_BATTERY ||
      s_bottom_slot == SLOT_BATTERY) {
    time_t now = time(NULL);
    refresh_all_slots(localtime(&now));
  }
}

// ---------------------------------------------------------------------------
// Health service handler
// ---------------------------------------------------------------------------

static void health_handler(HealthEventType event, void *context) {
  if (event == HealthEventHeartRateUpdate) {
    if (s_left_slot == SLOT_HEARTRATE || s_right_slot == SLOT_HEARTRATE ||
        s_bottom_slot == SLOT_HEARTRATE) {
      time_t now = time(NULL);
      refresh_all_slots(localtime(&now));
    }
  }
}

// ---------------------------------------------------------------------------
// AppMessage: Clay settings + weather data
// ---------------------------------------------------------------------------

static void resubscribe_tick(void) {
  tick_timer_service_unsubscribe();
  tick_timer_service_subscribe(
      s_display_seconds ? SECOND_UNIT : MINUTE_UNIT, tick_handler);
}

static void tint_bitmap(GBitmap *bmp, GColor color) {
  GColor *palette = gbitmap_get_palette(bmp);
  if (!palette) return;
  int n = 0;
  switch (gbitmap_get_format(bmp)) {
    case GBitmapFormat1BitPalette: n = 2;  break;
    case GBitmapFormat2BitPalette: n = 4;  break;
    case GBitmapFormat4BitPalette: n = 16; break;
    default: return;
  }
  for (int i = 0; i < n; i++) {
    if ((palette[i].argb & 0xC0) == 0xC0)
      palette[i] = color;
  }
}

static void apply_accent_color(void) {
  GColor c = s_accent_color;
  text_layer_set_text_color(s_left_text, c);
  text_layer_set_text_color(s_right_text, c);
  text_layer_set_text_color(s_bottom_text, c);

  tint_bitmap(s_logo_bitmap, c);
  bitmap_layer_set_bitmap(s_logo_layer, s_logo_bitmap);

  tint_bitmap(s_heart_bitmap, c);
  for (int i = 0; i < 6; i++)
    tint_bitmap(s_weather_bitmaps[i], c);
  tint_bitmap(s_battery_bitmap, c);
  tint_bitmap(s_steps_bitmap, c);

  layer_mark_dirty(s_battery_bar_layer);
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
  if ((t = dict_find(iter, MESSAGE_KEY_useFahrenheit))) {
    bool v = (bool)t->value->int32;
    if (v != s_use_fahrenheit) { s_use_fahrenheit = v; weather_changed = true; }
    persist_write_bool(PERSIST_USE_FAHRENHEIT, v);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_accentColor))) {
    uint8_t v = (uint8_t)t->value->int32;
    GColor nc = (GColor){ .argb = v };
    if (!gcolor_equal(nc, s_accent_color)) {
      s_accent_color = nc;
      apply_accent_color();
    }
    persist_write_int(PERSIST_ACCENT_COLOR, (int)v);
  }

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
  }
  if (weather_changed) {
    update_weather_icon();
  }
  if (layout_changed || style_changed || weather_changed) {
    time_t now = time(NULL);
    refresh_all_slots(localtime(&now));
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

  s_slot_font = fonts_load_custom_font(resource_get_handle(
      RESOURCE_ID_FONT_DIGITALDREAM_NARROW_16));

  // Bitmap background
  s_bg_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND);
  s_bg_bitmap_layer = bitmap_layer_create(bounds);
  bitmap_layer_set_bitmap(s_bg_bitmap_layer, s_bg_bitmap);
  bitmap_layer_set_compositing_mode(s_bg_bitmap_layer, GCompOpSet);
  layer_add_child(root, bitmap_layer_get_layer(s_bg_bitmap_layer));

  // Hand layers (drawn UNDER info slots)
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

  s_center_layer = layer_create(GRect(
      bounds.size.w / 2 - (CENTER_OUTER_R + 2),
      bounds.size.h / 2 - (CENTER_OUTER_R + 2),
      (CENTER_OUTER_R + 2) * 2,
      (CENTER_OUTER_R + 2) * 2));
  layer_set_update_proc(s_center_layer, center_update_proc);
  layer_add_child(root, s_center_layer);

  // Battery bar + pebble logo (above hands, below info text)
  s_battery_bar_layer = layer_create(GRect(BAT_BAR_X, BAT_BAR_Y - 1, BAT_BAR_W, 3));
  layer_set_update_proc(s_battery_bar_layer, battery_bar_update_proc);
  layer_add_child(root, s_battery_bar_layer);

  s_logo_bitmap = gbitmap_create_with_resource(RESOURCE_ID_PEBBLE_LOGO);
  s_logo_layer = bitmap_layer_create(GRect(LOGO_X, LOGO_Y, LOGO_W, LOGO_H));
  bitmap_layer_set_bitmap(s_logo_layer, s_logo_bitmap);
  bitmap_layer_set_compositing_mode(s_logo_layer, GCompOpSet);
  layer_add_child(root, bitmap_layer_get_layer(s_logo_layer));

  // Info slot text layers (on top of hands)
  s_left_text = text_layer_create(GRectZero);
  text_layer_set_font(s_left_text, s_slot_font);
  text_layer_set_overflow_mode(s_left_text, GTextOverflowModeWordWrap);
  text_layer_set_text_alignment(s_left_text, GTextAlignmentCenter);
  text_layer_set_background_color(s_left_text, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_left_text));

  s_right_text = text_layer_create(GRectZero);
  text_layer_set_font(s_right_text, s_slot_font);
  text_layer_set_overflow_mode(s_right_text, GTextOverflowModeWordWrap);
  text_layer_set_text_alignment(s_right_text, GTextAlignmentCenter);
  text_layer_set_background_color(s_right_text, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_right_text));

  s_bottom_text = text_layer_create(GRectZero);
  text_layer_set_font(s_bottom_text, s_slot_font);
  text_layer_set_overflow_mode(s_bottom_text, GTextOverflowModeWordWrap);
  text_layer_set_text_alignment(s_bottom_text, GTextAlignmentCenter);
  text_layer_set_background_color(s_bottom_text, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_bottom_text));

  // Icon bitmaps
  s_heart_bitmap = gbitmap_create_with_resource(RESOURCE_ID_ICON_HEART);
  s_weather_bitmaps[0] = gbitmap_create_with_resource(RESOURCE_ID_ICON_W_CLEAR);
  s_weather_bitmaps[1] = gbitmap_create_with_resource(RESOURCE_ID_ICON_W_PCLOUDY);
  s_weather_bitmaps[2] = gbitmap_create_with_resource(RESOURCE_ID_ICON_W_CLOUDY);
  s_weather_bitmaps[3] = gbitmap_create_with_resource(RESOURCE_ID_ICON_W_RAIN);
  s_weather_bitmaps[4] = gbitmap_create_with_resource(RESOURCE_ID_ICON_W_SNOW);
  s_weather_bitmaps[5] = gbitmap_create_with_resource(RESOURCE_ID_ICON_W_THUNDER);
  s_battery_bitmap = gbitmap_create_with_resource(RESOURCE_ID_ICON_BATTERY);
  s_steps_bitmap = gbitmap_create_with_resource(RESOURCE_ID_ICON_STEPS);

  // Icon bitmap layers (on top of text)
  s_hr_icon_layer = bitmap_layer_create(GRect(0, 0, HEART_W, HEART_H));
  bitmap_layer_set_bitmap(s_hr_icon_layer, s_heart_bitmap);
  bitmap_layer_set_compositing_mode(s_hr_icon_layer, GCompOpSet);
  layer_add_child(root, bitmap_layer_get_layer(s_hr_icon_layer));

  s_weather_icon_layer = bitmap_layer_create(GRect(0, 0, WICON_W, WICON_H));
  bitmap_layer_set_compositing_mode(s_weather_icon_layer, GCompOpSet);
  layer_add_child(root, bitmap_layer_get_layer(s_weather_icon_layer));

  s_bat_icon_layer = bitmap_layer_create(GRect(0, 0, BATT_W, BATT_H));
  bitmap_layer_set_bitmap(s_bat_icon_layer, s_battery_bitmap);
  bitmap_layer_set_compositing_mode(s_bat_icon_layer, GCompOpSet);
  layer_add_child(root, bitmap_layer_get_layer(s_bat_icon_layer));

  s_steps_icon_layer = bitmap_layer_create(GRect(0, 0, STEPS_W, STEPS_H));
  bitmap_layer_set_bitmap(s_steps_icon_layer, s_steps_bitmap);
  bitmap_layer_set_compositing_mode(s_steps_icon_layer, GCompOpSet);
  layer_add_child(root, bitmap_layer_get_layer(s_steps_icon_layer));

  position_slot_layers();
  apply_accent_color();

  // GPath objects pivoted to screen center
  s_hour_path      = gpath_create(&HOUR_HAND_INFO);
  s_hour_diamond   = gpath_create(&HOUR_DIAMOND_INFO);
  s_minute_path    = gpath_create(&MINUTE_HAND_INFO);
  s_minute_diamond = gpath_create(&MINUTE_DIAMOND_INFO);
  GPoint center = grect_center_point(&bounds);
  gpath_move_to(s_hour_path,      center);
  gpath_move_to(s_hour_diamond,   center);
  gpath_move_to(s_minute_path,    center);
  gpath_move_to(s_minute_diamond, center);

  time_t now = time(NULL);
  s_tick_time = *localtime(&now);
  refresh_all_slots(&s_tick_time);
}

static void window_unload(Window *window) {
  gpath_destroy(s_hour_path);
  gpath_destroy(s_hour_diamond);
  gpath_destroy(s_minute_path);
  gpath_destroy(s_minute_diamond);

  layer_destroy(s_center_layer);
  layer_destroy(s_second_layer);
  layer_destroy(s_minute_layer);
  layer_destroy(s_hour_layer);

  bitmap_layer_destroy(s_steps_icon_layer);
  bitmap_layer_destroy(s_bat_icon_layer);
  bitmap_layer_destroy(s_weather_icon_layer);
  bitmap_layer_destroy(s_hr_icon_layer);

  text_layer_destroy(s_bottom_text);
  text_layer_destroy(s_right_text);
  text_layer_destroy(s_left_text);

  gbitmap_destroy(s_steps_bitmap);
  gbitmap_destroy(s_battery_bitmap);
  for (int i = 0; i < 6; i++) {
    gbitmap_destroy(s_weather_bitmaps[i]);
  }
  gbitmap_destroy(s_heart_bitmap);

  bitmap_layer_destroy(s_logo_layer);
  gbitmap_destroy(s_logo_bitmap);
  layer_destroy(s_battery_bar_layer);
  bitmap_layer_destroy(s_bg_bitmap_layer);
  gbitmap_destroy(s_bg_bitmap);

  fonts_unload_custom_font(s_slot_font);
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
  if (persist_exists(PERSIST_WEATHER_TEMP))
    s_temperature = persist_read_int(PERSIST_WEATHER_TEMP);
  if (persist_exists(PERSIST_WEATHER_COND))
    s_weather_cond = persist_read_int(PERSIST_WEATHER_COND);
  if (persist_exists(PERSIST_HAS_WEATHER))
    s_has_weather = persist_read_bool(PERSIST_HAS_WEATHER);
  if (persist_exists(PERSIST_USE_FAHRENHEIT))
    s_use_fahrenheit = persist_read_bool(PERSIST_USE_FAHRENHEIT);
  if (persist_exists(PERSIST_ACCENT_COLOR))
    s_accent_color = (GColor){ .argb = (uint8_t)persist_read_int(PERSIST_ACCENT_COLOR) };
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
  health_service_events_subscribe(health_handler, NULL);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_open(512, 64);
}

static void deinit(void) {
  if (s_anim_timer) {
    app_timer_cancel(s_anim_timer);
    s_anim_timer = NULL;
  }
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  health_service_events_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
