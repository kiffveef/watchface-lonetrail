#pragma once

#include <pebble.h>

// 分変化の演出時間(仕様: 通常 500ms、折り返し 800ms)
#define LONETRAIL_ANIM_DURATION_MS 500
#define LONETRAIL_ANIM_WRAP_DURATION_MS 800

// この残量以下で「電池低下」表示に切り替える
#define LONETRAIL_BATTERY_LOW_PERCENT 20

// 3桁区切りの歩数("99,999")と "--" が収まる長さ
#define LONETRAIL_VALUE_BUF_LEN 12

// 日付の並びは両デザイン共通(例: Sep 17 / Thu)
#define LONETRAIL_FMT_DATE "%b %d"
#define LONETRAIL_FMT_WEEKDAY "%a"
#define LONETRAIL_DATE_BUF_LEN 8
#define LONETRAIL_TIME_BUF_LEN 8

typedef struct {
  // 直近の tick 時刻(演出の到達点)
  struct tm now;
  // 直前の tick 時刻(演出の開始点)。起動直後は now と同じ
  struct tm prev;
  // 負の値は取得不可(`--` 表示)
  int32_t steps;
  int32_t bpm;
  bool bt_connected;
  bool battery_low;
} LonetrailState;

typedef struct {
  GColor background_color;
  // Window の load / unload で呼ばれる。カスタムフォントや GPath の確保・解放用(NULL 可)
  void (*load)(void);
  void (*unload)(void);
  // 静的要素(路線・同心円など)。BT 状態変化時のみ再描画される
  void (*draw_background)(GContext *ctx, GRect bounds, const LonetrailState *state);
  // 分変化の演出で補間する値を時刻から求める(位置や角度を固定小数で返す)。
  // 値が前回より小さくなる分変化は「折り返し」として長い演出になる
  int32_t (*anim_value)(const struct tm *t);
  // 動的要素(時刻・マーカー・データ行)。anim_value は演出中は補間値、非演出時は now の値
  void (*draw_dynamic)(GContext *ctx, GRect bounds, const LonetrailState *state,
                       int32_t anim_value);
} LonetrailDesign;

typedef struct {
  GFont *slot;
  uint32_t resource_id;
} LonetrailFontSpec;

// Window 生成・購読・イベントループを実行し、終了時に解放する
void lonetrail_run(const LonetrailDesign *design);

void lonetrail_fonts_load(const LonetrailFontSpec *specs, size_t count);
void lonetrail_fonts_unload(const LonetrailFontSpec *specs, size_t count);

// 描画して、実際に文字が占めた矩形を返す(次の要素の位置決め用)
GRect lonetrail_draw_text(GContext *ctx, const char *text, GFont font, GRect box, GColor color,
                          GTextAlignment align);

// 12h / 24h 設定に従って "HH:MM" を書き込む
void lonetrail_format_time(char *buf, size_t len, const struct tm *t);

// 負の値は "--"、それ以外は3桁区切りで書き込む。値があれば true
bool lonetrail_format_value(char *buf, size_t len, int32_t value);
