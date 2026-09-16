#include "lonetrail.h"

static const LonetrailDesign *s_design;
static LonetrailState s_state;

static Window *s_window;
static Layer *s_background_layer;
static Layer *s_dynamic_layer;

// SDK3 の Animation は終了・unschedule 時に自動破棄されるため、stopped で NULL に戻す
static Animation *s_animation;
static AnimationProgress s_progress = ANIMATION_NORMALIZED_MAX;

int32_t lonetrail_lerp(int32_t from, int32_t to, AnimationProgress progress) {
  // 差分×progress が int32 を超えうる(角度を TRIG 単位で渡す場合)ため 64bit で計算する
  return from + (int32_t)((int64_t)(to - from) * progress / ANIMATION_NORMALIZED_MAX);
}

void lonetrail_format_value(char *buf, size_t len, int32_t value) {
  if (value < 0) {
    snprintf(buf, len, "--");
    return;
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
}

// --- Layers ---

static void prv_background_update_proc(Layer *layer, GContext *ctx) {
  s_design->draw_background(ctx, layer_get_bounds(layer), &s_state);
}

static void prv_dynamic_update_proc(Layer *layer, GContext *ctx) {
  s_design->draw_dynamic(ctx, layer_get_bounds(layer), &s_state, s_progress);
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
  bool wrap = s_design->is_wrap && s_design->is_wrap(&s_state.prev, &s_state.now);
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

static void prv_update_health(void) {
  time_t now = time(NULL);
  HealthServiceAccessibilityMask steps_mask =
      health_service_metric_accessible(HealthMetricStepCount, time_start_of_today(), now);
  s_state.steps = (steps_mask & HealthServiceAccessibilityMaskAvailable)
      ? health_service_sum_today(HealthMetricStepCount) : -1;

  HealthServiceAccessibilityMask bpm_mask =
      health_service_metric_accessible(HealthMetricHeartRateBPM, now, now);
  s_state.bpm = (bpm_mask & HealthServiceAccessibilityMaskAvailable)
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

static void prv_battery_handler(BatteryChargeState charge) {
  s_state.battery_low = charge.charge_percent <= LONETRAIL_BATTERY_LOW_PERCENT;
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
  s_state.battery_low =
      battery_state_service_peek().charge_percent <= LONETRAIL_BATTERY_LOW_PERCENT;
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
