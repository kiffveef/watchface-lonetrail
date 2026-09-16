#pragma once

#include <pebble.h>

// モールス符号の1単位(短点の幅・高さ、符号間の間隔)。長点は3単位、文字間3単位、語間7単位
#define MORSE_UNIT 2
#define MORSE_HEIGHT MORSE_UNIT

// 英数字と空白をモールス符号の矩形列として描く(左上基準、現在の fill 色)。対応外の文字は無視
void morse_draw_text(GContext *ctx, const char *text, GPoint origin);
