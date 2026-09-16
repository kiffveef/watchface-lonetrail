#pragma once

#include <pebble.h>

// 分変化の演出時間(仕様: 通常 500ms、折り返し 800ms)
#define LONETRAIL_ANIM_DURATION_MS 500
#define LONETRAIL_ANIM_WRAP_DURATION_MS 800

// この残量以下で「電池低下」表示に切り替える
#define LONETRAIL_BATTERY_LOW_PERCENT 20

// 値なし表示に必要な最小バッファ長("--" + NUL)
#define LONETRAIL_VALUE_BUF_LEN 12

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
  // Window の load / unload で呼ばれる。カスタムフォントの確保・解放用(NULL 可)
  void (*load)(void);
  void (*unload)(void);
  // 静的要素(路線・同心円など)。BT 状態変化時のみ再描画される
  void (*draw_background)(GContext *ctx, GRect bounds, const LonetrailState *state);
  // 動的要素(時刻・マーカー・データ行)。progress は演出中 MIN〜MAX、非演出時 MAX
  void (*draw_dynamic)(GContext *ctx, GRect bounds, const LonetrailState *state,
                       AnimationProgress progress);
  // prev→now の変化を長い演出(折り返し)にするか。NULL なら常に通常時間
  bool (*is_wrap)(const struct tm *prev, const struct tm *now);
} LonetrailDesign;

// Window 生成・購読・イベントループを実行し、終了時に解放する
void lonetrail_run(const LonetrailDesign *design);

// from→to を progress(ANIMATION_NORMALIZED_MIN〜MAX)で線形補間する
int32_t lonetrail_lerp(int32_t from, int32_t to, AnimationProgress progress);

// 負の値は "--"、それ以外は3桁区切りで書き込む
void lonetrail_format_value(char *buf, size_t len, int32_t value);
