#include "font.h"
#include <string.h>

/* --- MICRO, the HUD font from my OWL FLY game ---
 * first 41 glyphs are resource 1 of CITY.DAT, copied byte for byte.
 * 6 rows per glyph, bits 7..5 are the 3 columns. signs after '.' are new,
 * drawn for the radar in the same 3x5 grid */
static const uint8_t MICRO[][6] = {
    {0xE0,0xA0,0xA0,0xA0,0xE0,0x00}, /* '0' */
    {0x40,0xC0,0x40,0x40,0xE0,0x00}, /* '1' */
    {0xE0,0x20,0xE0,0x80,0xE0,0x00}, /* '2' */
    {0xE0,0x20,0xE0,0x20,0xE0,0x00}, /* '3' */
    {0xA0,0xA0,0xE0,0x20,0x20,0x00}, /* '4' */
    {0xE0,0x80,0xE0,0x20,0xE0,0x00}, /* '5' */
    {0xE0,0x80,0xE0,0xA0,0xE0,0x00}, /* '6' */
    {0xE0,0x20,0x40,0x40,0x40,0x00}, /* '7' */
    {0xE0,0xA0,0xE0,0xA0,0xE0,0x00}, /* '8' */
    {0xE0,0xA0,0xE0,0x20,0xE0,0x00}, /* '9' */
    {0x40,0xA0,0xE0,0xA0,0xA0,0x00}, /* 'A' */
    {0xC0,0xA0,0xC0,0xA0,0xC0,0x00}, /* 'B' */
    {0x60,0x80,0x80,0x80,0x60,0x00}, /* 'C' */
    {0xC0,0xA0,0xA0,0xA0,0xC0,0x00}, /* 'D' */
    {0xE0,0x80,0xC0,0x80,0xE0,0x00}, /* 'E' */
    {0xE0,0x80,0xC0,0x80,0x80,0x00}, /* 'F' */
    {0x60,0x80,0xA0,0xA0,0x60,0x00}, /* 'G' */
    {0xA0,0xA0,0xE0,0xA0,0xA0,0x00}, /* 'H' */
    {0xE0,0x40,0x40,0x40,0xE0,0x00}, /* 'I' */
    {0x20,0x20,0x20,0xA0,0x40,0x00}, /* 'J' */
    {0xA0,0xC0,0x80,0xC0,0xA0,0x00}, /* 'K' */
    {0x80,0x80,0x80,0x80,0xE0,0x00}, /* 'L' */
    {0xA0,0xE0,0xA0,0xA0,0xA0,0x00}, /* 'M' */
    {0xE0,0xA0,0xA0,0xA0,0xA0,0x00}, /* 'N' */
    {0xE0,0xA0,0xA0,0xA0,0xE0,0x00}, /* 'O' */
    {0xE0,0xA0,0xE0,0x80,0x80,0x00}, /* 'P' */
    {0xE0,0xA0,0xA0,0xE0,0x20,0x00}, /* 'Q' */
    {0xC0,0xA0,0xC0,0xA0,0xA0,0x00}, /* 'R' */
    {0xE0,0x80,0xE0,0x20,0xE0,0x00}, /* 'S' */
    {0xE0,0x40,0x40,0x40,0x40,0x00}, /* 'T' */
    {0xA0,0xA0,0xA0,0xA0,0xE0,0x00}, /* 'U' */
    {0xA0,0xA0,0xA0,0xA0,0x40,0x00}, /* 'V' */
    {0xA0,0xA0,0xA0,0xE0,0xA0,0x00}, /* 'W' */
    {0xA0,0xA0,0x40,0xA0,0xA0,0x00}, /* 'X' */
    {0xA0,0xA0,0x40,0x40,0x40,0x00}, /* 'Y' */
    {0xE0,0x20,0x40,0x80,0xE0,0x00}, /* 'Z' */
    {0x00,0x40,0xE0,0x40,0x00,0x00}, /* '+' */
    {0x00,0x00,0xE0,0x00,0x00,0x00}, /* '-' */
    {0x00,0x40,0x00,0x40,0x00,0x00}, /* ':' */
    {0xA0,0x20,0x40,0x80,0xA0,0x00}, /* '%' */
    {0x00,0x00,0x00,0x00,0x40,0x00}, /* '.' */
    /* --- drawn for RadarScan --- */
    {0x20,0x20,0x40,0x80,0x80,0x00}, /* '/' */
    {0x40,0xA0,0x40,0x00,0x00,0x00}, /* '*' as a degree sign */
    {0x20,0x40,0x80,0x40,0x20,0x00}, /* '<' */
    {0x80,0x40,0x20,0x40,0x80,0x00}, /* '>' */
    {0x00,0xE0,0x00,0xE0,0x00,0x00}, /* '=' */
    {0x40,0x80,0x80,0x80,0x40,0x00}, /* '(' */
    {0x40,0x20,0x20,0x20,0x40,0x00}, /* ')' */
    {0x40,0x40,0x40,0x00,0x40,0x00}, /* '!' */
    {0xE0,0x20,0x60,0x00,0x40,0x00}, /* '?' */
    {0x00,0x00,0x00,0x00,0x40,0x80}, /* ',' */
    {0x40,0x40,0x00,0x00,0x00,0x00}, /* '\'' */
    {0xC0,0x80,0x80,0x80,0xC0,0x00}, /* '[' */
    {0x60,0x20,0x20,0x20,0x60,0x00}, /* ']' */
    {0x00,0x00,0x00,0x00,0xE0,0x00}, /* '_' */
    {0xA0,0xE0,0xA0,0xE0,0xA0,0x00}, /* '#' */
    {0x00,0x00,0xC0,0x60,0x00,0x00}, /* '~' */
    {0x40,0x40,0x40,0x40,0x40,0x00}, /* '|' */
    {0x40,0xE0,0x00,0x00,0x00,0x00}, /* '^' */
};

static int micro_index(unsigned char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 10;   // no lower case, use caps
    static const char signs[] = "+-:%./*<>=()!?,'[]_#~|^";
    const char *p = strchr(signs, ch);
    return (p && ch) ? 36 + (int)(p - signs) : -1;      // careful: strchr finds the '\0' too
}

CORE_HOT int micro_text(canvas_t *cv, int x, int y, const char *s, uint32_t c, int a, int scale)
{
    int x0 = x;
    if (!gfx_touches(cv, y, y + MICRO_H * scale)) return micro_width(s, scale);   // not in this strip, just measure
    for (; *s; s++, x += MICRO_ADV * scale) {
        int gi = micro_index((unsigned char)*s);
        if (gi < 0) continue;
        const uint8_t *g = MICRO[gi];
        for (int r = 0; r < 6; r++) {
            uint8_t bits = g[r];
            if (!bits) continue;
            for (int sy = 0; sy < scale; sy++) {
                uint32_t *row = gfx_row(cv, y + r * scale + sy);
                if (!row) continue;
                for (int col = 0; col < 3; col++) {
                    if (!(bits & (0x80 >> col))) continue;
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        if (px >= 0 && px < GFX_W)
                            row[px] = a >= 256 ? c : gfx_mix(row[px], c, (uint32_t)a);
                    }
                }
            }
        }
    }
    return x - x0 - scale;   // no gap after the last char
}

/* --- UTF-8 and the baked fonts --- */

uint32_t utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = *p++;
    int more = 0;
    if (c < 0x80) more = 0;
    else if ((c & 0xE0) == 0xC0) { c &= 0x1F; more = 1; }
    else if ((c & 0xF0) == 0xE0) { c &= 0x0F; more = 2; }
    else if ((c & 0xF8) == 0xF0) { c &= 0x07; more = 3; }
    else { *s = (const char *)p; return '?'; }
    while (more--) {
        if ((*p & 0xC0) != 0x80) { *s = (const char *)p; return '?'; }
        c = (c << 6) | (*p++ & 0x3F);
    }
    *s = (const char *)p;
    return c;
}

static const glyph_t *find_glyph(const font_t *f, uint32_t cp)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint32_t m = f->glyphs[mid].cp;
        if (m == cp) return &f->glyphs[mid];
        if (m < cp) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

static const glyph_t *glyph_or_fallback(const font_t *f, uint32_t cp)
{
    const glyph_t *g = find_glyph(f, cp);
    if (!g && cp >= 0x80) g = find_glyph(f, '?');   // emoji and such
    return g;
}

/* text loop for all baked fonts. limit = first column that must stay clear (or big number).
 * NOTE: cut names fade out over the last FADE px instead of "...", at six pixels
 * ellipsis looks like underscore and "Byt_Sokol_" could be a real network name */
#define FADE 12
CORE_HOT static int draw_text(canvas_t *cv, const font_t *f, int x, int y, const char *utf8, uint32_t c, int a,
                     int limit, int fade)
{
    int x0 = x;
    int hit = gfx_touches(cv, y, y + f->line_h);
    while (*utf8) {
        const glyph_t *g = glyph_or_fallback(f, utf8_next(&utf8));
        if (!g) continue;
        if (x + g->xo >= limit) break;
        if (hit && g->w) {
            int stride = (g->w + 1) >> 1;
            const uint8_t *bits = f->bitmap + g->off;
            for (int r = 0; r < g->h; r++) {
                uint32_t *row = gfx_row(cv, y + g->yo + r);
                if (!row) continue;
                const uint8_t *b = bits + r * stride;
                for (int col = 0; col < g->w; col++) {
                    unsigned v = (b[col >> 1] >> ((col & 1) ? 0 : 4)) & 15;
                    if (!v) continue;
                    int px = x + g->xo + col;
                    if (px < 0 || px >= GFX_W || px >= limit) continue;
                    int k = (a * (int)(v * 17 + 1)) >> 8;   // v*17+1: 15 -> 256
                    // paper: the soft edges stay, paper.c rounds them to the panel's two greys
                    // (no dither), that is the halftone smoothing. no fade there, see font_text_clip
                    if (!RADAR_EPAPER && fade && px > limit - FADE) k = k * (limit - px) / FADE;
                    if (k > 0) row[px] = gfx_mix(row[px], c, (uint32_t)k);
                }
            }
        }
        x += g->adv;
    }
    return (x < limit ? x : limit) - x0;
}

int font_text(canvas_t *cv, const font_t *f, int x, int y, const char *utf8, uint32_t c, int a)
{
    return draw_text(cv, f, x, y, utf8, c, a, 1 << 20, 0);   // no limit
}

int font_text_clip(canvas_t *cv, const font_t *f, int x, int y, const char *utf8, uint32_t c, int a, int maxw)
{
    int cut = font_width(f, utf8) > maxw;
    if (RADAR_EPAPER && cut) {
        // paper can't fade, so whole letters and an ellipsis. at these sizes it can't pass for "_"
        int ew = font_width(f, "\xE2\x80\xA6");
        int w = 0;
        const char *s = utf8;
        while (*s) {                                  // letters that fit before the ellipsis
            const char *at = s;
            const glyph_t *g = glyph_or_fallback(f, utf8_next(&s));
            if (g && w + g->adv > maxw - ew) { s = at; break; }
            if (g) w += g->adv;
        }
        draw_text(cv, f, x, y, utf8, c, a, x + w, 0);
        draw_text(cv, f, x + w, y, "\xE2\x80\xA6", c, a, 1 << 20, 0);
        return w + ew;
    }
    return draw_text(cv, f, x, y, utf8, c, a, x + maxw, cut);
}

int font_width(const font_t *f, const char *utf8)
{
    int w = 0;
    while (*utf8) {
        const glyph_t *g = glyph_or_fallback(f, utf8_next(&utf8));
        if (g) w += g->adv;
    }
    return w;
}
