#include "orbit.h"
#include "design_select.h"

#if LONETRAIL_USE_ORBIT

// 座標はすべて emery(200x228)前提の仕様値
#define ORBIT_BG_COLOR GColorPastelYellow
#define ORBIT_CENTER_X (-60)
#define ORBIT_CENTER_Y 114
#define ORBIT_RING_WIDTH 5
#define ORBIT_RING_HALF (ORBIT_RING_WIDTH / 2)
#define ORBIT_TRACK_RADIUS 112

// 月の軌道: 分 0→59 で -54°→+54°(画面座標系、+x が 0°、時計回り)
#define ORBIT_ANGLE_START_DEG (-54)
#define ORBIT_ANGLE_SWEEP_DEG 108
#define ORBIT_MINUTES_PER_HOUR 60
// 分を細分化して補間する(TRIG_MAX_ANGLE × 59分 × 分割数 が int32 に収まる範囲)
#define ORBIT_MINUTE_SUBDIV 64
#define ORBIT_MINUTE_SCALE (ORBIT_MINUTES_PER_HOUR * ORBIT_MINUTE_SUBDIV)
#define ORBIT_MOON_RADIUS 6

// 文字はすべて右端 x=188 に右揃え
#define ORBIT_TEXT_RIGHT 188
#define ORBIT_DAY_Y 59
#define ORBIT_DAY_H 24
#define ORBIT_WEEKDAY_GAP 4
#define ORBIT_TIME_RECT GRect(40, 70, 148, 60)
#define ORBIT_STEPS_UNIT_RECT GRect(100, 146, 88, 16)
#define ORBIT_STEPS_RECT GRect(80, 158, 108, 22)
#define ORBIT_BPM_UNIT_RECT GRect(100, 182, 88, 16)
#define ORBIT_BPM_RECT GRect(80, 194, 108, 22)

typedef struct {
  int16_t radius;
  uint8_t argb;
} OrbitRing;

static const OrbitRing s_rings[] = {
  { 86, GColorDarkCandyAppleRedARGB8 },
  { 92, GColorChromeYellowARGB8 },
  { 98, GColorCadetBlueARGB8 },
};

// 月の各走査線の半幅 w = √(R²−dy²)、dy = −6〜6
static const int8_t s_moon_half_width[ORBIT_MOON_RADIUS * 2 + 1] = {
  0, 3, 4, 5, 6, 6, 6, 6, 6, 5, 4, 3, 0,
};

// フォントは Route と同じ系統。時刻は幅の狭い Condensed で大きく、値は余裕があるので 16
static GFont s_font_time;
static GFont s_font_date;
static GFont s_font_value;
static GFont s_font_unit;

static const LonetrailFontSpec s_fonts[] = {
  { &s_font_time, RESOURCE_ID_FONT_BARLOW_CONDENSED_BOLD_56 },
  { &s_font_date, RESOURCE_ID_FONT_INTER_REGULAR_16 },
  { &s_font_value, RESOURCE_ID_FONT_INTER_BOLD_16 },
  { &s_font_unit, RESOURCE_ID_FONT_INTER_REGULAR_12 },
};

static void prv_load(void) {
  lonetrail_fonts_load(s_fonts, ARRAY_LENGTH(s_fonts));
}

static void prv_unload(void) {
  lonetrail_fonts_unload(s_fonts, ARRAY_LENGTH(s_fonts));
}

static GRect prv_circle_rect(int16_t outer) {
  return GRect(ORBIT_CENTER_X - outer, ORBIT_CENTER_Y - outer, outer * 2 + 1, outer * 2 + 1);
}

static void prv_draw_background(GContext *ctx, GRect bounds, const LonetrailState *state) {
  // 同心円は線幅を確実に反映させるため環状塗りで描く(Route の円弧と同じ理由)
  for (size_t i = 0; i < ARRAY_LENGTH(s_rings); i++) {
    GColor color = state->bt_connected ? (GColor) { .argb = s_rings[i].argb } : GColorLightGray;
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_radial(ctx, prv_circle_rect(s_rings[i].radius + ORBIT_RING_HALF),
                         GOvalScaleModeFitCircle, ORBIT_RING_WIDTH, 0, TRIG_MAX_ANGLE);
  }
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, GPoint(ORBIT_CENTER_X, ORBIT_CENTER_Y), ORBIT_TRACK_RADIUS);
}

// 演出で補間する値: 分 × 分割数。毎時の折り返し(59→0)は値が減るので長い演出になる
static int32_t prv_anim_value(const struct tm *t) {
  return t->tm_min * ORBIT_MINUTE_SUBDIV;
}

static GPoint prv_moon_center(int32_t minute_scaled) {
  int32_t angle = DEG_TO_TRIGANGLE(ORBIT_ANGLE_START_DEG)
      + DEG_TO_TRIGANGLE(ORBIT_ANGLE_SWEEP_DEG) * minute_scaled / ORBIT_MINUTE_SCALE;
  return GPoint(ORBIT_CENTER_X + cos_lookup(angle) * ORBIT_TRACK_RADIUS / TRIG_MAX_RATIO,
                ORBIT_CENTER_Y + sin_lookup(angle) * ORBIT_TRACK_RADIUS / TRIG_MAX_RATIO);
}

// 位相 φ = 分/60 を 1 時間周期で満ち欠けさせる(0: 新月、0.5: 満月)。実月齢ではない
static void prv_draw_moon(GContext *ctx, GPoint center, int32_t minute_scaled, bool battery_low) {
  GColor color = battery_low ? GColorDarkCandyAppleRed : GColorBlack;
  // 背景レイヤーの軌道線が月の中を通らないよう、先に背景色で塗りつぶす
  graphics_context_set_fill_color(ctx, ORBIT_BG_COLOR);
  graphics_fill_circle(ctx, center, ORBIT_MOON_RADIUS);
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, center, ORBIT_MOON_RADIUS);

  int32_t phase_angle = TRIG_MAX_ANGLE * minute_scaled / ORBIT_MINUTE_SCALE;
  bool waxing = phase_angle < TRIG_MAX_ANGLE / 2;
  int32_t cos_phase = cos_lookup(phase_angle);
  graphics_context_set_fill_color(ctx, color);
  for (int dy = -ORBIT_MOON_RADIUS; dy <= ORBIT_MOON_RADIUS; dy++) {
    int16_t w = s_moon_half_width[dy + ORBIT_MOON_RADIUS];
    // 明暗境界。上弦は右端から左へ、下弦は右端が左へ欠けていく
    int16_t x_t = w * cos_phase / TRIG_MAX_RATIO;
    int16_t x0 = waxing ? x_t : -w;
    int16_t x1 = waxing ? w : -x_t;
    if (x1 > x0) {
      graphics_fill_rect(ctx, GRect(center.x + x0, center.y + dy, x1 - x0, 1), 0, GCornerNone);
    }
  }
}

// 右揃えで描き、描画した文字列の左端 x を返す
static int16_t prv_draw_text_right(GContext *ctx, const char *text, GFont font, GRect box,
                                   GColor color) {
  return lonetrail_draw_text(ctx, text, font, box, color, GTextAlignmentRight).origin.x;
}

static void prv_draw_texts(GContext *ctx, const LonetrailState *state) {
  const struct tm *now = &state->now;

  // 月 日 曜日 の順(例: Sep 17 Thu)。右端の曜日から左へ詰める。
  // 実機では DarkGray の曜日が掠れて見えるため月日と同じ黒にする
  char weekday_buf[LONETRAIL_DATE_BUF_LEN];
  strftime(weekday_buf, sizeof(weekday_buf), LONETRAIL_FMT_WEEKDAY, now);
  int16_t weekday_left = prv_draw_text_right(ctx, weekday_buf, s_font_date,
      GRect(0, ORBIT_DAY_Y, ORBIT_TEXT_RIGHT, ORBIT_DAY_H), GColorBlack);
  char date_buf[LONETRAIL_DATE_BUF_LEN];
  strftime(date_buf, sizeof(date_buf), LONETRAIL_FMT_DATE, now);
  prv_draw_text_right(ctx, date_buf, s_font_date,
      GRect(0, ORBIT_DAY_Y, weekday_left - ORBIT_WEEKDAY_GAP, ORBIT_DAY_H), GColorBlack);

  char time_buf[LONETRAIL_TIME_BUF_LEN];
  lonetrail_format_time(time_buf, sizeof(time_buf), now);
  prv_draw_text_right(ctx, time_buf, s_font_time, ORBIT_TIME_RECT, GColorBlack);

  char steps_buf[LONETRAIL_VALUE_BUF_LEN];
  char bpm_buf[LONETRAIL_VALUE_BUF_LEN];
  bool has_steps = lonetrail_format_value(steps_buf, sizeof(steps_buf), state->steps);
  bool has_bpm = lonetrail_format_value(bpm_buf, sizeof(bpm_buf), state->bpm);
  prv_draw_text_right(ctx, "steps", s_font_unit, ORBIT_STEPS_UNIT_RECT, GColorDarkGray);
  prv_draw_text_right(ctx, steps_buf, s_font_value, ORBIT_STEPS_RECT,
                      has_steps ? GColorBlack : GColorDarkGray);
  prv_draw_text_right(ctx, "bpm", s_font_unit, ORBIT_BPM_UNIT_RECT, GColorDarkGray);
  prv_draw_text_right(ctx, bpm_buf, s_font_value, ORBIT_BPM_RECT,
                      has_bpm ? GColorBlack : GColorDarkGray);
}

static void prv_draw_dynamic(GContext *ctx, GRect bounds, const LonetrailState *state,
                             int32_t anim_value) {
  prv_draw_moon(ctx, prv_moon_center(anim_value), anim_value, state->battery_low);
  prv_draw_texts(ctx, state);
}

const LonetrailDesign ORBIT_DESIGN = {
  .background_color = ORBIT_BG_COLOR,
  .load = prv_load,
  .unload = prv_unload,
  .draw_background = prv_draw_background,
  .anim_value = prv_anim_value,
  .draw_dynamic = prv_draw_dynamic,
};

#endif
