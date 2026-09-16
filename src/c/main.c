#include <pebble.h>

#define TIME_LAYER_HEIGHT 50
#define DATE_LAYER_HEIGHT 26
#define TIME_TO_DATE_GAP 4

static Window *s_window;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;

static char s_time_buffer[8];
static char s_date_buffer[16];

static void prv_update_time(struct tm *tick_time) {
  strftime(s_time_buffer, sizeof(s_time_buffer),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);

  strftime(s_date_buffer, sizeof(s_date_buffer), "%a %b %d", tick_time);
  text_layer_set_text(s_date_layer, s_date_buffer);
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_time(tick_time);
}

static TextLayer *prv_create_text_layer(Layer *parent, GRect frame, const char *font_key) {
  TextLayer *layer = text_layer_create(frame);
  text_layer_set_background_color(layer, GColorClear);
  text_layer_set_text_color(layer, GColorWhite);
  text_layer_set_font(layer, fonts_get_system_font(font_key));
  text_layer_set_text_alignment(layer, GTextAlignmentCenter);
  layer_add_child(parent, text_layer_get_layer(layer));
  return layer;
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // 時刻と日付をまとめて画面中央に縦配置する
  int16_t block_height = TIME_LAYER_HEIGHT + TIME_TO_DATE_GAP + DATE_LAYER_HEIGHT;
  int16_t time_y = (bounds.size.h - block_height) / 2;
  int16_t date_y = time_y + TIME_LAYER_HEIGHT + TIME_TO_DATE_GAP;

  s_time_layer = prv_create_text_layer(window_layer,
      GRect(0, time_y, bounds.size.w, TIME_LAYER_HEIGHT), FONT_KEY_BITHAM_42_BOLD);
  s_date_layer = prv_create_text_layer(window_layer,
      GRect(0, date_y, bounds.size.w, DATE_LAYER_HEIGHT), FONT_KEY_GOTHIC_24);
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  // 起動直後に空表示にならないよう初回描画を行う
  time_t now = time(NULL);
  prv_update_time(localtime(&now));
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
