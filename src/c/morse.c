#include "morse.h"

#define MORSE_DOT_W MORSE_UNIT
#define MORSE_DASH_W (MORSE_UNIT * 3)
#define MORSE_SYMBOL_GAP MORSE_UNIT
#define MORSE_LETTER_GAP (MORSE_UNIT * 3)
#define MORSE_WORD_GAP (MORSE_UNIT * 7)

static const char *const s_alpha_codes[26] = {
  ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
  "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",
};

static const char *const s_digit_codes[10] = {
  "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.",
};

static const char *prv_code_for(char c) {
  if (c >= 'A' && c <= 'Z') {
    return s_alpha_codes[c - 'A'];
  }
  if (c >= 'a' && c <= 'z') {
    return s_alpha_codes[c - 'a'];
  }
  if (c >= '0' && c <= '9') {
    return s_digit_codes[c - '0'];
  }
  return NULL;
}

void morse_draw_text(GContext *ctx, const char *text, GPoint origin) {
  int16_t x = origin.x;
  for (const char *p = text; *p != '\0'; p++) {
    if (*p == ' ') {
      // 直前の文字間(3単位)に足して語間(7単位)にする
      x += MORSE_WORD_GAP - MORSE_LETTER_GAP;
      continue;
    }
    const char *code = prv_code_for(*p);
    if (!code) {
      continue;
    }
    for (const char *s = code; *s != '\0'; s++) {
      int16_t w = (*s == '-') ? MORSE_DASH_W : MORSE_DOT_W;
      graphics_fill_rect(ctx, GRect(x, origin.y, w, MORSE_HEIGHT), 0, GCornerNone);
      x += w + MORSE_SYMBOL_GAP;
    }
    x += MORSE_LETTER_GAP - MORSE_SYMBOL_GAP;
  }
}
