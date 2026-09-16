#include "lonetrail.h"

// Route / Orbit 実装までの仮デザイン。共通基盤の各機能を目視確認するためのもの
#define PH_LINE_Y 120
#define PH_DOT_RADIUS 5
#define PH_TIME_RECT GRect(0, 30, 200, 50)
#define PH_DATE_RECT GRect(0, 80, 200, 22)
#define PH_DATA_RECT GRect(0, 190, 200, 20)

static void prv_draw_background(GContext *ctx, GRect bounds, const LonetrailState *state) {
  graphics_context_set_stroke_color(ctx, state->bt_connected ? GColorChromeYellow : GColorDarkGray);
  graphics_context_set_stroke_width(ctx, 4);
  graphics_draw_line(ctx, GPoint(0, PH_LINE_Y), GPoint(bounds.size.w, PH_LINE_Y));
}

static void prv_draw_text(GContext *ctx, GRect rect, const char *font_key, const char *text) {
  graphics_draw_text(ctx, text, fonts_get_system_font(font_key), rect,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void prv_draw_dynamic(GContext *ctx, GRect bounds, const LonetrailState *state,
                             AnimationProgress progress) {
  // 分に応じて左端→右端へ動く点。折り返し(59→0)は逆方向に戻る
  int32_t x = lonetrail_lerp(state->prev.tm_min * bounds.size.w / 60,
                             state->now.tm_min * bounds.size.w / 60, progress);
  graphics_context_set_fill_color(ctx, state->battery_low ? GColorDarkCandyAppleRed : GColorWhite);
  graphics_fill_circle(ctx, GPoint(x, PH_LINE_Y), PH_DOT_RADIUS);

  char time_buf[8];
  strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", &state->now);
  char date_buf[16];
  strftime(date_buf, sizeof(date_buf), "%a %d %b", &state->now);
  char steps_buf[LONETRAIL_VALUE_BUF_LEN];
  lonetrail_format_value(steps_buf, sizeof(steps_buf), state->steps);
  char bpm_buf[LONETRAIL_VALUE_BUF_LEN];
  lonetrail_format_value(bpm_buf, sizeof(bpm_buf), state->bpm);
  char data_buf[LONETRAIL_VALUE_BUF_LEN * 2 + 16];
  snprintf(data_buf, sizeof(data_buf), "%s steps  %s bpm", steps_buf, bpm_buf);

  graphics_context_set_text_color(ctx, GColorWhite);
  prv_draw_text(ctx, PH_TIME_RECT, FONT_KEY_LECO_42_NUMBERS, time_buf);
  prv_draw_text(ctx, PH_DATE_RECT, FONT_KEY_GOTHIC_18, date_buf);
  prv_draw_text(ctx, PH_DATA_RECT, FONT_KEY_GOTHIC_14_BOLD, data_buf);
}

static bool prv_is_wrap(const struct tm *prev, const struct tm *now) {
  return now->tm_min < prev->tm_min;
}

static const LonetrailDesign s_placeholder_design = {
  .background_color = GColorBlack,
  .draw_background = prv_draw_background,
  .draw_dynamic = prv_draw_dynamic,
  .is_wrap = prv_is_wrap,
};

int main(void) {
  lonetrail_run(&s_placeholder_design);
}
