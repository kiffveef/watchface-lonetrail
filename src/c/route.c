#include "route.h"

// 座標はすべて emery(200x228)前提の仕様値
#define ROUTE_LINE_WIDTH 4
// Pebble は偶数の線幅を次の奇数に丸めて描くため、実際の太さは 5px
#define ROUTE_LINE_RENDERED_WIDTH (ROUTE_LINE_WIDTH | 1)
#define ROUTE_LINE_HALF (ROUTE_LINE_RENDERED_WIDTH / 2)
#define ROUTE_ARC_CENTER_X 70
#define ROUTE_ARC_CENTER_Y 164
#define ROUTE_ARC_START_DEG 180
#define ROUTE_ARC_END_DEG 270

#define ROUTE_MINUTES_PER_DAY 1440

// 駅マーカーは中央線(黄)の上を、縦→曲線→横と辿る。距離は経路に沿った px
#define ROUTE_PATH_X 36
#define ROUTE_PATH_START_Y 10
#define ROUTE_PATH_ARC_RADIUS 34
#define ROUTE_PATH_END_X 188
#define ROUTE_PATH_VERTICAL_LEN (ROUTE_ARC_CENTER_Y - ROUTE_PATH_START_Y)
// 四分円の弧長 2π·34/4 ≈ 53.4
#define ROUTE_PATH_ARC_LEN 53
#define ROUTE_PATH_HORIZONTAL_LEN (ROUTE_PATH_END_X - ROUTE_ARC_CENTER_X)
#define ROUTE_PATH_TOTAL_LEN \
  (ROUTE_PATH_VERTICAL_LEN + ROUTE_PATH_ARC_LEN + ROUTE_PATH_HORIZONTAL_LEN)

#define ROUTE_MARKER_W 24
#define ROUTE_MARKER_H 12
#define ROUTE_MARKER_RADIUS 6
#define ROUTE_MARKER_STROKE 2
#define ROUTE_MARKER_CORE_HALF 2
// ピル両端の半円を何分割した多角形にするか
#define ROUTE_MARKER_ARC_STEPS 12

// 時刻ブロック(曜日+日付+時刻)の見た目の縦中心は t + OFFSET(Jost の余白を実測)
#define ROUTE_TIME_BLOCK_CENTER_OFFSET 15
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

// 時刻は DIN 系の Barlow、日付・データ行は Inter(いずれも Google Fonts / OFL)
#define ROUTE_FONT_TIME RESOURCE_ID_FONT_BARLOW_BOLD_48
#define ROUTE_FONT_WEEKDAY RESOURCE_ID_FONT_INTER_REGULAR_16
#define ROUTE_FONT_DATE RESOURCE_ID_FONT_INTER_REGULAR_16
#define ROUTE_FONT_VALUE RESOURCE_ID_FONT_INTER_BOLD_12
#define ROUTE_FONT_UNIT RESOURCE_ID_FONT_INTER_REGULAR_12

// カスタムフォントは load で確保し unload で解放する
static GFont s_font_time;
static GFont s_font_weekday;
static GFont s_font_date;
static GFont s_font_value;
static GFont s_font_unit;

// 進行方向に合わせて回転させるため、マーカーは原点中心の GPath で持つ
static GPoint s_marker_body_points[(ROUTE_MARKER_ARC_STEPS + 1) * 2];
static GPoint s_marker_core_points[] = {
  { -ROUTE_MARKER_CORE_HALF, -ROUTE_MARKER_CORE_HALF },
  { ROUTE_MARKER_CORE_HALF, -ROUTE_MARKER_CORE_HALF },
  { ROUTE_MARKER_CORE_HALF, ROUTE_MARKER_CORE_HALF },
  { -ROUTE_MARKER_CORE_HALF, ROUTE_MARKER_CORE_HALF },
};
static const GPathInfo s_marker_body_info = {
  .num_points = ARRAY_LENGTH(s_marker_body_points),
  .points = s_marker_body_points,
};
static const GPathInfo s_marker_core_info = {
  .num_points = ARRAY_LENGTH(s_marker_core_points),
  .points = s_marker_core_points,
};
static GPath *s_marker_body;
static GPath *s_marker_core;

// 角丸ピル(24x12、角丸6)を時計回りの多角形で近似する。長軸は x 方向
static void prv_build_marker_body(void) {
  int16_t half_w = ROUTE_MARKER_W / 2 - ROUTE_MARKER_RADIUS;
  for (int i = 0; i <= ROUTE_MARKER_ARC_STEPS; i++) {
    int32_t right = DEG_TO_TRIGANGLE(180 * i / ROUTE_MARKER_ARC_STEPS);
    int32_t left = right + DEG_TO_TRIGANGLE(180);
    s_marker_body_points[i] = GPoint(
        half_w + sin_lookup(right) * ROUTE_MARKER_RADIUS / TRIG_MAX_RATIO,
        -cos_lookup(right) * ROUTE_MARKER_RADIUS / TRIG_MAX_RATIO);
    s_marker_body_points[ROUTE_MARKER_ARC_STEPS + 1 + i] = GPoint(
        -half_w + sin_lookup(left) * ROUTE_MARKER_RADIUS / TRIG_MAX_RATIO,
        -cos_lookup(left) * ROUTE_MARKER_RADIUS / TRIG_MAX_RATIO);
  }
}

static void prv_load(void) {
  prv_build_marker_body();
  s_marker_body = gpath_create(&s_marker_body_info);
  s_marker_core = gpath_create(&s_marker_core_info);
  s_font_time = fonts_load_custom_font(resource_get_handle(ROUTE_FONT_TIME));
  s_font_weekday = fonts_load_custom_font(resource_get_handle(ROUTE_FONT_WEEKDAY));
  s_font_date = fonts_load_custom_font(resource_get_handle(ROUTE_FONT_DATE));
  s_font_value = fonts_load_custom_font(resource_get_handle(ROUTE_FONT_VALUE));
  s_font_unit = fonts_load_custom_font(resource_get_handle(ROUTE_FONT_UNIT));
}

static void prv_unload(void) {
  gpath_destroy(s_marker_body);
  gpath_destroy(s_marker_core);
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

typedef struct {
  GPoint pos;
  // マーカーの回転角(TRIG 単位、gpath_rotate_to に渡す)。縦区間 0、横区間 -90°
  int32_t angle;
} RoutePose;

// 経路に沿った距離 d(0〜TOTAL_LEN)から位置と向きを求める
static RoutePose prv_marker_pose(int32_t d) {
  if (d < ROUTE_PATH_VERTICAL_LEN) {
    return (RoutePose) { GPoint(ROUTE_PATH_X, ROUTE_PATH_START_Y + d), 0 };
  }
  d -= ROUTE_PATH_VERTICAL_LEN;
  if (d < ROUTE_PATH_ARC_LEN) {
    // 左端(270°)から真下(180°)へ回り込む。長軸を半径方向に保つよう同じ角度だけ回す
    int32_t turn = TRIG_MAX_ANGLE / 4 * d / ROUTE_PATH_ARC_LEN;
    int32_t angle = DEG_TO_TRIGANGLE(270) - turn;
    return (RoutePose) {
      GPoint(ROUTE_ARC_CENTER_X + sin_lookup(angle) * ROUTE_PATH_ARC_RADIUS / TRIG_MAX_RATIO,
             ROUTE_ARC_CENTER_Y - cos_lookup(angle) * ROUTE_PATH_ARC_RADIUS / TRIG_MAX_RATIO),
      -turn,
    };
  }
  d -= ROUTE_PATH_ARC_LEN;
  if (d > ROUTE_PATH_HORIZONTAL_LEN) {
    d = ROUTE_PATH_HORIZONTAL_LEN;
  }
  return (RoutePose) {
    GPoint(ROUTE_ARC_CENTER_X + d, ROUTE_ARC_CENTER_Y + ROUTE_PATH_ARC_RADIUS),
    -TRIG_MAX_ANGLE / 4,
  };
}

static int32_t prv_marker_distance(const LonetrailState *state, AnimationProgress progress) {
  // 分単位で補間すると1分の移動(約0.2px)が段になるため、経路長倍のまま補間して最後に割る
  int32_t scaled = lonetrail_lerp(prv_minute_of_day(&state->prev) * ROUTE_PATH_TOTAL_LEN,
                                  prv_minute_of_day(&state->now) * ROUTE_PATH_TOTAL_LEN,
                                  progress);
  return scaled / ROUTE_MINUTES_PER_DAY;
}

static void prv_draw_marker(GContext *ctx, RoutePose pose, bool battery_low) {
  gpath_move_to(s_marker_body, pose.pos);
  gpath_rotate_to(s_marker_body, pose.angle);
  gpath_move_to(s_marker_core, pose.pos);
  gpath_rotate_to(s_marker_core, pose.angle);

  graphics_context_set_fill_color(ctx, GColorWhite);
  gpath_draw_filled(ctx, s_marker_body);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, ROUTE_MARKER_STROKE);
  gpath_draw_outline(ctx, s_marker_body);

  graphics_context_set_fill_color(ctx, battery_low ? GColorDarkCandyAppleRed : GColorBlack);
  gpath_draw_filled(ctx, s_marker_core);
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
      GRect(weekday_x, date_y, bounds.size.w - weekday_x, ROUTE_DATE_H), GColorLightGray);
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
  prv_draw_marker(ctx, prv_marker_pose(prv_marker_distance(state, progress)), state->battery_low);
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
