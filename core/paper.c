#include "paper.h"
#include <stdlib.h>

/* the 4 greys as the glass shows them, measured in ViewOwl (display-types.json).
 * the 4.2" pulls its two greys towards black and white, the 5.79" keeps them apart */
const uint8_t PAPER_LEVEL[4] = { 0, PAPER_DARK, PAPER_LIGHT, 255 };

/* every pixel goes to the NEAREST of the 4 greys, no dither. dithering made text and
 * thin lines fuzzy on the real panel, and dots that differ from frame to frame would
 * spoil the partial refresh (epd.c). the screens are drawn in exactly these greys
 * anyway (INK_* in paper.h), only edges have to be rounded */

#define LO 14       // light below this is background, stays clean white

static uint8_t s_code[256];         // paper luminance -> code
static int     s_built;

static void build(void)
{
    for (int p = 0; p < 256; p++) {
        int best = 0;
        for (int k = 1; k < 4; k++)
            if (abs(p - PAPER_LEVEL[k]) < abs(p - PAPER_LEVEL[best])) best = k;
        s_code[p] = (uint8_t)best;
    }
    s_built = 1;
}

void paper_pack(const canvas_t *cv, uint8_t *fb)
{
    if (!s_built) build();
    for (int r = 0; r < cv->h; r++) {
        const uint32_t *row = cv->px + r * GFX_W;
        uint8_t *out = fb + (cv->y0 + r) * PAPER_STRIDE;
        uint8_t acc = 0;
        for (int x = 0; x < GFX_W; x++) {
            uint32_t q = row[x];
            int l = (int)((((q >> 16) & 255) * 77 + ((q >> 8) & 255) * 150 + (q & 255) * 29) >> 8);
            acc = (uint8_t)(acc << 2 | s_code[l < LO ? 255 : 255 - l]);   // negative: light is ink
            if ((x & 3) == 3) { out[x >> 2] = acc; acc = 0; }
        }
        if (GFX_W & 3) {                                     // pad the last byte with white
            int left = 4 - (GFX_W & 3);
            out[GFX_W >> 2] = (uint8_t)(acc << (2 * left) | ((1 << (2 * left)) - 1));
        }
    }
}
