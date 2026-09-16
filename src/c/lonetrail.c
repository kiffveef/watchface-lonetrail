#include "lonetrail.h"

static const LonetrailDesign *s_design;
static LonetrailState s_state;

static Window *s_window;
static Layer *s_background_layer;
static Layer *s_dynamic_layer;

// SDK3 の Animation は終了・unschedule 時に自動破棄されるため、stopped で NULL に戻す
static Animation *s_animation;
static AnimationProgress s_progress = ANIMATION_NORMALIZED_MAX;

// --- Helpers for designs ---

void lonetrail_fonts_load(const LonetrailFontSpec *specs, size_t count) {
  for (size_t i = 0; i < count; i++) {
    *specs[i].slot = fonts_load_custom_font(resource_get_handle(specs[i].resource_id));
  }
}

void lonetrail_fonts_unload(const LonetrailFontSpec *specs, size_t count) {
  for (size_t i = 0; i < count; i++) {
    fonts_unload_custom_font(*specs[i].slot);
  }
}

GRect lonetrail_draw_text(GContext *ctx, const char *text, GFont font, GRect box, GColor color,
                          GTextAlignment align) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis, align, NULL);
  GSize size = graphics_text_layout_get_content_size(text, font, box,
      GTextOverflowModeTrailingEllipsis, align);
  int16_t x = box.origin.x;
  if (align == GTextAlignmentRight) {
    x = box.origin.x + box.size.w - size.w;
  } else if (align == GTextAlignmentCenter) {
    x = box.origin.x + (box.size.w - size.w) / 2;
  }
  return GRect(x, box.origin.y, size.w, size.h);
}

void lonetrail_format_time(char *buf, size_t len, const struct tm *t) {
  strftime(buf, len, clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
}

bool lonetrail_format_value(char *buf, size_t len, int32_t value) {
  if (value < 0) {
    snprintf(buf, len, "--");
    return false;
  }
  // 末尾から桁を詰め、3桁ごとにカンマを挟む
  char tmp[16];
  int pos = sizeof(tmp) - 1;
  tmp[pos] = '\0';
  int digits = 0;
  do {
    if (digits > 0 && digits % 3 == 0) {
      tmp[--pos] = ',';
    }
    tmp[--pos] = '0' + value % 10;
    value /= 10;
    digits++;
  } while (value > 0);
  snprintf(buf, len, "%s", &tmp[pos]);
  return true;
}

// --- Layers ---

static void prv_background_update_proc(Layer *layer, GContext *ctx) {
  s_design->draw_background(ctx, layer_get_bounds(layer), &s_state);
}

static void prv_dynamic_update_proc(Layer *layer, GContext *ctx) {
  int32_t from = s_design->anim_value(&s_state.prev);
  int32_t to = s_design->anim_value(&s_state.now);
  // 差分×progress が int32 を超えうる(角度を TRIG 単位で渡す場合)ため 64bit で計算する
  int32_t value = from + (int32_t)((int64_t)(to - from) * s_progress / ANIMATION_NORMALIZED_MAX);
  s_design->draw_dynamic(ctx, layer_get_bounds(layer), &s_state, value);
}

// --- Animation ---

static void prv_animation_update(Animation *animation, const AnimationProgress progress) {
  s_progress = progress;
  layer_mark_dirty(s_dynamic_layer);
}

static const AnimationImplementation s_animation_impl = {
  .update = prv_animation_update,
};

static void prv_animation_stopped(Animation *animation, bool finished, void *context) {
  s_animation = NULL;
  s_progress = ANIMATION_NORMALIZED_MAX;
  layer_mark_dirty(s_dynamic_layer);
}

static void prv_start_animation(void) {
  if (s_animation) {
    animation_unschedule(s_animation);
  }
  bool wrap = s_design->anim_value(&s_state.now) < s_design->anim_value(&s_state.prev);
  s_animation = animation_create();
  animation_set_implementation(s_animation, &s_animation_impl);
  animation_set_duration(s_animation,
      wrap ? LONETRAIL_ANIM_WRAP_DURATION_MS : LONETRAIL_ANIM_DURATION_MS);
  animation_set_curve(s_animation, AnimationCurveEaseInOut);
  animation_set_handlers(s_animation, (AnimationHandlers) {
    .stopped = prv_animation_stopped,
  }, NULL);
  s_progress = ANIMATION_NORMALIZED_MIN;
  animation_schedule(s_animation);
}

// --- Services ---

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  s_state.prev = s_state.now;
  s_state.now = *tick_time;
  prv_start_animation();
}

static bool prv_metric_accessible(HealthMetric metric, time_t start, time_t end) {
  return health_service_metric_accessible(metric, start, end)
      & HealthServiceAccessibilityMaskAvailable;
}

static void prv_update_health(void) {
  time_t now = time(NULL);
  s_state.steps = prv_metric_accessible(HealthMetricStepCount, time_start_of_today(), now)
      ? health_service_sum_today(HealthMetricStepCount) : -1;
  s_state.bpm = prv_metric_accessible(HealthMetricHeartRateBPM, now, now)
      ? health_service_peek_current_value(HealthMetricHeartRateBPM) : -1;
  // 心拍 0 はセンサー未計測なので「値なし」として扱う
  if (s_state.bpm == 0) {
    s_state.bpm = -1;
  }
}

static void prv_health_handler(HealthEventType event, void *context) {
  if (event == HealthEventSleepUpdate) {
    return;
  }
  prv_update_health();
  layer_mark_dirty(s_dynamic_layer);
}

static void prv_connection_handler(bool connected) {
  if (s_state.bt_connected && !connected) {
    vibes_short_pulse();
  }
  s_state.bt_connected = connected;
  layer_mark_dirty(s_background_layer);
  layer_mark_dirty(s_dynamic_layer);
}

static bool prv_is_battery_low(BatteryChargeState charge) {
  return charge.charge_percent <= LONETRAIL_BATTERY_LOW_PERCENT;
}

static void prv_battery_handler(BatteryChargeState charge) {
  s_state.battery_low = prv_is_battery_low(charge);
  layer_mark_dirty(s_dynamic_layer);
}

// --- Window ---

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  if (s_design->load) {
    s_design->load();
  }

  s_background_layer = layer_create(bounds);
  layer_set_update_proc(s_background_layer, prv_background_update_proc);
  layer_add_child(window_layer, s_background_layer);

  s_dynamic_layer = layer_create(bounds);
  layer_set_update_proc(s_dynamic_layer, prv_dynamic_update_proc);
  layer_add_child(window_layer, s_dynamic_layer);
}

static void prv_window_unload(Window *window) {
  if (s_animation) {
    animation_unschedule(s_animation);
  }
  layer_destroy(s_dynamic_layer);
  layer_destroy(s_background_layer);
  if (s_design->unload) {
    s_design->unload();
  }
}

static void prv_init(void) {
  // 起動時は演出なしで最終位置に描くため prev = now にする
  time_t now = time(NULL);
  s_state.now = *localtime(&now);
  s_state.prev = s_state.now;
  s_state.bt_connected = connection_service_peek_pebble_app_connection();
  s_state.battery_low = prv_is_battery_low(battery_state_service_peek());
  s_state.steps = -1;
  s_state.bpm = -1;
  if (health_service_events_subscribe(prv_health_handler, NULL)) {
    prv_update_health();
  }

  s_window = window_create();
  window_set_background_color(s_window, s_design->background_color);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = prv_connection_handler,
  });
  battery_state_service_subscribe(prv_battery_handler);
}

static void prv_deinit(void) {
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  tick_timer_service_unsubscribe();
  health_service_events_unsubscribe();
  window_destroy(s_window);
}

void lonetrail_run(const LonetrailDesign *design) {
  s_design = design;
  prv_init();
  app_event_loop();
  prv_deinit();
}
