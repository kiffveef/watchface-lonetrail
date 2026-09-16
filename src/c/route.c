#include "route.h"

// 座標はすべて emery(200x228)前提の仕様値
#define ROUTE_LINE_WIDTH 4
// Pebble は偶数の線幅を次の奇数に丸めて描くため、実際の太さは 5px
#define ROUTE_LINE_RENDERED_WIDTH (ROUTE_LINE_WIDTH | 1)
#define ROUTE_LINE_HALF (ROUTE_LINE_RENDERED_WIDTH / 2)
#define ROUTE_ARC_CENTER_X 70
#define ROUTE_ARC_CENTER_Y 170
#define ROUTE_ARC_START_DEG 180
#define ROUTE_ARC_END_DEG 270

#define ROUTE_MINUTES_PER_DAY 1440
#define ROUTE_MARKER_Y_MIN 10
#define ROUTE_MARKER_Y_RANGE 150
#define ROUTE_MARKER_X 24
#define ROUTE_MARKER_W 24
#define ROUTE_MARKER_H 12
#define ROUTE_MARKER_RADIUS 6
#define ROUTE_MARKER_STROKE 2
#define ROUTE_MARKER_CORE_X 34
#define ROUTE_MARKER_CORE_SIZE 4

// 時刻ブロック(曜日+日付+時刻)の見た目の縦中心は t + OFFSET(Jost の余白を実測)
#define ROUTE_TIME_BLOCK_CENTER_OFFSET 12
#define ROUTE_TIME_X 54
#define ROUTE_TIME_W 146
#define ROUTE_TIME_H 50
#define ROUTE_DATE_X 58
#define ROUTE_DATE_Y_OFFSET 22
#define ROUTE_DATE_H 20
#define ROUTE_DATE_GAP 4

#define ROUTE_DATA_X 68
#define ROUTE_DATA_Y 212
#define ROUTE_DATA_H 16
#define ROUTE_DATA_UNIT_GAP 3
#define ROUTE_DATA_ITEM_GAP 10

typedef struct {
  int16_t x;
  int16_t radius;
  uint8_t argb;
} RouteLine;

// 縦線 x / 曲がりの半径 / 色。横線の y は ARC_CENTER_Y + radius
static const RouteLine s_lines[] = {
  { 30, 40, GColorDarkCandyAppleRedARGB8 },
  { 36, 34, GColorChromeYellowARGB8 },
  { 42, 28, GColorCadetBlueARGB8 },
};

// カスタムフォントは load で確保し unload で解放する
static GFont s_font_time;
static GFont s_font_weekday;
static GFont s_font_date;
static GFont s_font_value;
static GFont s_font_unit;

static void prv_load(void) {
  s_font_time = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_JOST_BOLD_42));
  s_font_weekday = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_JOST_REGULAR_18));
  s_font_date = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_JOST_BOLD_18));
  s_font_value = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_JOST_BOLD_12));
  s_font_unit = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_JOST_REGULAR_12));
}

static void prv_unload(void) {
  fonts_unload_custom_font(s_font_time);
  fonts_unload_custom_font(s_font_weekday);
  fonts_unload_custom_font(s_font_date);
  fonts_unload_custom_font(s_font_value);
  fonts_unload_custom_font(s_font_unit);
}

static void prv_draw_background(GContext *ctx, GRect bounds, const LonetrailState *state) {
  // アンチエイリアスは隣接する路線との1px 隙間を不揃いに見せるため切る
  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_stroke_width(ctx, ROUTE_LINE_WIDTH);
  for (size_t i = 0; i < ARRAY_LENGTH(s_lines); i++) {
    const RouteLine *line = &s_lines[i];
    GColor color = state->bt_connected ? (GColor) { .argb = line->argb } : GColorDarkGray;
    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_fill_color(ctx, color);

    int16_t horizontal_y = ROUTE_ARC_CENTER_Y + line->radius;
    graphics_draw_line(ctx, GPoint(line->x, 0), GPoint(line->x, ROUTE_ARC_CENTER_Y));
    // draw_arc は線幅を反映しないため、直線と同じ太さの環状扇形で曲線を描く
    int16_t outer = line->radius + ROUTE_LINE_HALF;
    graphics_fill_radial(ctx,
        GRect(ROUTE_ARC_CENTER_X - outer, ROUTE_ARC_CENTER_Y - outer, outer * 2 + 1, outer * 2 + 1),
        GOvalScaleModeFitCircle, ROUTE_LINE_RENDERED_WIDTH,
        DEG_TO_TRIGANGLE(ROUTE_ARC_START_DEG), DEG_TO_TRIGANGLE(ROUTE_ARC_END_DEG));
    graphics_draw_line(ctx, GPoint(ROUTE_ARC_CENTER_X, horizontal_y),
                       GPoint(bounds.size.w, horizontal_y));
  }
}

static int32_t prv_minute_of_day(const struct tm *t) {
  return t->tm_hour * 60 + t->tm_min;
}

static int16_t prv_marker_y(const LonetrailState *state, AnimationProgress progress) {
  // 分単位で補間すると1分の移動(約0.1px)が段になるため、150倍したまま補間して最後に割る
  int32_t scaled = lonetrail_lerp(prv_minute_of_day(&state->prev) * ROUTE_MARKER_Y_RANGE,
                                  prv_minute_of_day(&state->now) * ROUTE_MARKER_Y_RANGE,
                                  progress);
  return ROUTE_MARKER_Y_MIN + scaled / ROUTE_MINUTES_PER_DAY;
}

static void prv_draw_marker(GContext *ctx, int16_t y_s, bool battery_low) {
  GRect body = GRect(ROUTE_MARKER_X, y_s - ROUTE_MARKER_H / 2, ROUTE_MARKER_W, ROUTE_MARKER_H);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, body, ROUTE_MARKER_RADIUS, GCornersAll);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, ROUTE_MARKER_STROKE);
  graphics_draw_round_rect(ctx, body, ROUTE_MARKER_RADIUS);

  graphics_context_set_fill_color(ctx, battery_low ? GColorDarkCandyAppleRed : GColorBlack);
  graphics_fill_rect(ctx,
      GRect(ROUTE_MARKER_CORE_X, y_s - ROUTE_MARKER_CORE_SIZE / 2,
            ROUTE_MARKER_CORE_SIZE, ROUTE_MARKER_CORE_SIZE),
      0, GCornerNone);
}

static int16_t prv_text_width(GContext *ctx, const char *text, GFont font, GRect box) {
  return graphics_text_layout_get_content_size(text, font, box,
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft).w;
}

// 左揃えで描画し、次の要素の開始 x を返す
static int16_t prv_draw_text_left(GContext *ctx, const char *text, GFont font,
                                  GRect box, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  return box.origin.x + prv_text_width(ctx, text, font, box);
}

static void prv_draw_time_block(GContext *ctx, GRect bounds, const struct tm *now) {
  int16_t t = bounds.size.h / 2 - ROUTE_TIME_BLOCK_CENTER_OFFSET;

  char time_buf[8];
  strftime(time_buf, sizeof(time_buf), clock_is_24h_style() ? "%H:%M" : "%I:%M", now);
  prv_draw_text_left(ctx, time_buf, s_font_time,
                     GRect(ROUTE_TIME_X, t, ROUTE_TIME_W, ROUTE_TIME_H), GColorWhite);

  // 月 日 曜日 の順(例: Sep 17 Thu)
  char date_buf[8];
  strftime(date_buf, sizeof(date_buf), "%b %d", now);
  int16_t date_y = t - ROUTE_DATE_Y_OFFSET;
  int16_t weekday_x = prv_draw_text_left(ctx, date_buf, s_font_date,
      GRect(ROUTE_DATE_X, date_y, bounds.size.w - ROUTE_DATE_X, ROUTE_DATE_H), GColorWhite)
      + ROUTE_DATE_GAP;

  char weekday_buf[8];
  strftime(weekday_buf, sizeof(weekday_buf), "%a", now);
  prv_draw_text_left(ctx, weekday_buf, s_font_weekday,
      GRect(weekday_x, date_y, bounds.size.w - weekday_x, ROUTE_DATE_H), GColorWhite);
}

static void prv_draw_data_row(GContext *ctx, GRect bounds, const LonetrailState *state) {
  char steps_buf[LONETRAIL_VALUE_BUF_LEN];
  char bpm_buf[LONETRAIL_VALUE_BUF_LEN];
  lonetrail_format_value(steps_buf, sizeof(steps_buf), state->steps);
  lonetrail_format_value(bpm_buf, sizeof(bpm_buf), state->bpm);

  const struct {
    const char *text;
    GFont font;
    bool is_value;
    int16_t gap_after;
  } items[] = {
    { steps_buf, s_font_value, state->steps >= 0, ROUTE_DATA_UNIT_GAP },
    { "steps", s_font_unit, false, ROUTE_DATA_ITEM_GAP },
    { bpm_buf, s_font_value, state->bpm >= 0, ROUTE_DATA_UNIT_GAP },
    { "bpm", s_font_unit, false, 0 },
  };

  int16_t x = ROUTE_DATA_X;
  for (size_t i = 0; i < ARRAY_LENGTH(items); i++) {
    x = prv_draw_text_left(ctx, items[i].text, items[i].font,
        GRect(x, ROUTE_DATA_Y, bounds.size.w - x, ROUTE_DATA_H),
        items[i].is_value ? GColorWhite : GColorLightGray) + items[i].gap_after;
  }
}

static void prv_draw_dynamic(GContext *ctx, GRect bounds, const LonetrailState *state,
                             AnimationProgress progress) {
  int16_t y_s = prv_marker_y(state, progress);
  prv_draw_marker(ctx, y_s, state->battery_low);
  prv_draw_time_block(ctx, bounds, &state->now);
  prv_draw_data_row(ctx, bounds, state);
}

// 日付変化(23:59→0:00)で先頭へ戻るときだけ長い演出にする
static bool prv_is_wrap(const struct tm *prev, const struct tm *now) {
  return prv_minute_of_day(now) < prv_minute_of_day(prev);
}

const LonetrailDesign ROUTE_DESIGN = {
  .background_color = GColorBlack,
  .load = prv_load,
  .unload = prv_unload,
  .draw_background = prv_draw_background,
  .draw_dynamic = prv_draw_dynamic,
  .is_wrap = prv_is_wrap,
};
