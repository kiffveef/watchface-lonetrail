#include "route.h"
#include "morse.h"
#include "design_select.h"

#if !LONETRAIL_USE_ORBIT

// 座標はすべて emery(200x228)前提の仕様値
// 線幅は奇数で指定する(Pebble は偶数幅を次の奇数に丸めるため、4 と書いても 5 で描かれる)
#define ROUTE_LINE_WIDTH 5
#define ROUTE_LINE_HALF (ROUTE_LINE_WIDTH / 2)
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

// 時刻ブロック(月日+曜日+時刻)。t は見た目の縦中心が画面中央に来る値(実測)
#define ROUTE_TIME_T 96
#define ROUTE_TIME_RECT GRect(54, ROUTE_TIME_T, 146, 50)
#define ROUTE_DATE_X 58
#define ROUTE_DATE_Y (ROUTE_TIME_T - 14)
#define ROUTE_DATE_H 20
#define ROUTE_DATE_GAP 4

// 状態のモールス表示(時刻の下)。BT切断・電池低下はこの表示だけで、他の描画は変えない
#define ROUTE_MORSE_MARK_X 58
#define ROUTE_MORSE_MARK_SIZE 4
#define ROUTE_MORSE_TEXT_X 66
#define ROUTE_MORSE_Y 154
#define ROUTE_MORSE_ROW_GAP 7

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
static GFont s_font_time;
static GFont s_font_date;
static GFont s_font_value;
static GFont s_font_unit;

static const LonetrailFontSpec s_fonts[] = {
  { &s_font_time, RESOURCE_ID_FONT_BARLOW_BOLD_48 },
  { &s_font_date, RESOURCE_ID_FONT_INTER_REGULAR_16 },
  { &s_font_value, RESOURCE_ID_FONT_INTER_BOLD_12 },
  { &s_font_unit, RESOURCE_ID_FONT_INTER_REGULAR_12 },
};

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
  lonetrail_fonts_load(s_fonts, ARRAY_LENGTH(s_fonts));
}

static void prv_unload(void) {
  gpath_destroy(s_marker_body);
  gpath_destroy(s_marker_core);
  lonetrail_fonts_unload(s_fonts, ARRAY_LENGTH(s_fonts));
}

static void prv_draw_background(GContext *ctx, GRect bounds, const LonetrailState *state) {
  // アンチエイリアスは隣接する路線との1px 隙間を不揃いに見せるため切る
  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_stroke_width(ctx, ROUTE_LINE_WIDTH);
  for (size_t i = 0; i < ARRAY_LENGTH(s_lines); i++) {
    const RouteLine *line = &s_lines[i];
    GColor color = (GColor) { .argb = line->argb };
    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_fill_color(ctx, color);

    int16_t horizontal_y = ROUTE_ARC_CENTER_Y + line->radius;
    graphics_draw_line(ctx, GPoint(line->x, 0), GPoint(line->x, ROUTE_ARC_CENTER_Y));
    // draw_arc は線幅を反映しないため、直線と同じ太さの環状扇形で曲線を描く
    int16_t outer = line->radius + ROUTE_LINE_HALF;
    graphics_fill_radial(ctx,
        GRect(ROUTE_ARC_CENTER_X - outer, ROUTE_ARC_CENTER_Y - outer, outer * 2 + 1, outer * 2 + 1),
        GOvalScaleModeFitCircle, ROUTE_LINE_WIDTH,
        DEG_TO_TRIGANGLE(ROUTE_ARC_START_DEG), DEG_TO_TRIGANGLE(ROUTE_ARC_END_DEG));
    graphics_draw_line(ctx, GPoint(ROUTE_ARC_CENTER_X, horizontal_y),
                       GPoint(bounds.size.w, horizontal_y));
  }
}

// 演出で補間する値: 0時からの経過分 × 経路長(分単位だと1分の移動 約0.2px が段になるため)
static int32_t prv_anim_value(const struct tm *t) {
  return (t->tm_hour * 60 + t->tm_min) * ROUTE_PATH_TOTAL_LEN;
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

static void prv_draw_marker(GContext *ctx, RoutePose pose) {
  gpath_move_to(s_marker_body, pose.pos);
  gpath_rotate_to(s_marker_body, pose.angle);
  gpath_move_to(s_marker_core, pose.pos);
  gpath_rotate_to(s_marker_core, pose.angle);

  graphics_context_set_fill_color(ctx, GColorWhite);
  gpath_draw_filled(ctx, s_marker_body);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, ROUTE_MARKER_STROKE);
  gpath_draw_outline(ctx, s_marker_body);

  graphics_context_set_fill_color(ctx, GColorBlack);
  gpath_draw_filled(ctx, s_marker_core);
}

// 左揃えで描き、次の要素の開始 x を返す
static int16_t prv_draw_text_left(GContext *ctx, const char *text, GFont font, GRect box,
                                  GColor color) {
  GRect drawn = lonetrail_draw_text(ctx, text, font, box, color, GTextAlignmentLeft);
  return drawn.origin.x + drawn.size.w;
}

static void prv_draw_time_block(GContext *ctx, GRect bounds, const struct tm *now) {
  char time_buf[LONETRAIL_TIME_BUF_LEN];
  lonetrail_format_time(time_buf, sizeof(time_buf), now);
  prv_draw_text_left(ctx, time_buf, s_font_time, ROUTE_TIME_RECT, GColorWhite);

  char date_buf[LONETRAIL_DATE_BUF_LEN];
  strftime(date_buf, sizeof(date_buf), LONETRAIL_FMT_DATE, now);
  int16_t weekday_x = prv_draw_text_left(ctx, date_buf, s_font_date,
      GRect(ROUTE_DATE_X, ROUTE_DATE_Y, bounds.size.w - ROUTE_DATE_X, ROUTE_DATE_H), GColorWhite)
      + ROUTE_DATE_GAP;

  char weekday_buf[LONETRAIL_DATE_BUF_LEN];
  strftime(weekday_buf, sizeof(weekday_buf), LONETRAIL_FMT_WEEKDAY, now);
  prv_draw_text_left(ctx, weekday_buf, s_font_date,
      GRect(weekday_x, ROUTE_DATE_Y, bounds.size.w - weekday_x, ROUTE_DATE_H), GColorLightGray);
}

static void prv_draw_data_row(GContext *ctx, GRect bounds, const LonetrailState *state) {
  char steps_buf[LONETRAIL_VALUE_BUF_LEN];
  char bpm_buf[LONETRAIL_VALUE_BUF_LEN];
  bool has_steps = lonetrail_format_value(steps_buf, sizeof(steps_buf), state->steps);
  bool has_bpm = lonetrail_format_value(bpm_buf, sizeof(bpm_buf), state->bpm);

  const struct {
    const char *text;
    GFont font;
    bool is_value;
    int16_t gap_after;
  } items[] = {
    { steps_buf, s_font_value, has_steps, ROUTE_DATA_UNIT_GAP },
    { "steps", s_font_unit, false, ROUTE_DATA_ITEM_GAP },
    { bpm_buf, s_font_value, has_bpm, ROUTE_DATA_UNIT_GAP },
    { "bpm", s_font_unit, false, 0 },
  };

  int16_t x = ROUTE_DATA_X;
  for (size_t i = 0; i < ARRAY_LENGTH(items); i++) {
    x = prv_draw_text_left(ctx, items[i].text, items[i].font,
        GRect(x, ROUTE_DATA_Y, bounds.size.w - x, ROUTE_DATA_H),
        items[i].is_value ? GColorWhite : GColorLightGray) + items[i].gap_after;
  }
}

static void prv_draw_morse_row(GContext *ctx, const char *text, int16_t y) {
  graphics_fill_rect(ctx,
      GRect(ROUTE_MORSE_MARK_X, y - (ROUTE_MORSE_MARK_SIZE - MORSE_HEIGHT) / 2,
            ROUTE_MORSE_MARK_SIZE, ROUTE_MORSE_MARK_SIZE),
      0, GCornerNone);
  morse_draw_text(ctx, text, GPoint(ROUTE_MORSE_TEXT_X, y));
}

static void prv_draw_status_morse(GContext *ctx, const LonetrailState *state) {
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  int16_t y = ROUTE_MORSE_Y;
  if (!state->bt_connected) {
    prv_draw_morse_row(ctx, "NO LINK", y);
    y += ROUTE_MORSE_ROW_GAP;
  }
  if (state->battery_low) {
    prv_draw_morse_row(ctx, "LOW BAT", y);
  }
}

static void prv_draw_dynamic(GContext *ctx, GRect bounds, const LonetrailState *state,
                             int32_t anim_value) {
  prv_draw_status_morse(ctx, state);
  prv_draw_marker(ctx, prv_marker_pose(anim_value / ROUTE_MINUTES_PER_DAY));
  prv_draw_time_block(ctx, bounds, &state->now);
  prv_draw_data_row(ctx, bounds, state);
}

const LonetrailDesign ROUTE_DESIGN = {
  .background_color = GColorBlack,
  .load = prv_load,
  .unload = prv_unload,
  .draw_background = prv_draw_background,
  .anim_value = prv_anim_value,
  .draw_dynamic = prv_draw_dynamic,
};

#endif
