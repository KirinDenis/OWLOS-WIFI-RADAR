/* text, two kinds of fonts:
 * MICRO - 3x5 HUD font from my OWL FLY game (4x6 cell, 1 bit, caps/digits/few signs, int scale).
 *   used for instrument text: ring labels, degrees, channels, status lines.
 * AA fonts (font_*.c) - baked from TTF by tools/FontBake, 4 bit coverage. for network names,
 *   wich can be anything UTF-8: lower case, Slovak, Cyrillic... */
#pragma once
#include "gfx.h"

typedef struct {
    uint16_t cp;            /* Unicode code point */
    uint8_t  w, h;          /* ink box */
    int8_t   xo, yo;        /* offset from pen, yo is from line top */
    uint8_t  adv;           /* pen advance */
    uint8_t  pad;
    uint32_t off;           /* offset in bitmap. rows of ceil(w/2) bytes, high nibble first */
} glyph_t;

typedef struct {
    const glyph_t *glyphs;  /* sorted by cp, for binary search */
    uint16_t       count;
    uint8_t        line_h;  /* line height */
    uint8_t        ascent;  /* baseline, counted from line top */
    const uint8_t *bitmap;
} font_t;

extern const font_t FONT_SMALL;   // names in list and labels
extern const font_t FONT_MEDIUM;  // name of the tracked network
extern const font_t FONT_LARGE;   // big numbers
extern const font_t FONT_HUGE;    // target signal strength, full screen
// e-paper dashboard: DejaVu Sans Bold, read from across the room like ViewOwl's plates
extern const font_t FONT_PAPER_S;  // ~10 px, small labels
extern const font_t FONT_PAPER_M;  // ~13 px, names
extern const font_t FONT_PAPER_L;  // ~20 px, title, dBm
extern const font_t FONT_PAPER_XL; // ~34 px, the one big number
extern const font_t FONT_PAPER_XXL;// ~63 px, digits, minus and "~ M" only: the target's dBm

// next code point from UTF-8 string, bad bytes give '?'
uint32_t utf8_next(const char **s);

// x = pen, y = line top. returns advance in pixels
int font_text(canvas_t *cv, const font_t *f, int x, int y, const char *utf8, uint32_t c, int a);
int font_width(const font_t *f, const char *utf8);
// draws max maxw pixels, a cut string fades out on the right
int font_text_clip(canvas_t *cv, const font_t *f, int x, int y, const char *utf8, uint32_t c, int a, int maxw);

// MICRO: 3x5 glyph in a 4x6 cell, scale is integer
#define MICRO_ADV 4
#define MICRO_H   6
int micro_text(canvas_t *cv, int x, int y, const char *s, uint32_t c, int a, int scale);
static inline int micro_width(const char *s, int scale)
{
    int n = 0;
    while (*s++) n++;
    return n ? (n * MICRO_ADV - 1) * scale : 0;
}
