/* paper - the picture for e-paper, 4 greys, 2 bits a pixel.
 * the radar is drawn as light on dark glass, e-paper is ink on white, so this prints
 * the negative: dark background becomes white paper, bright lines become ink.
 * light 85 / 170 / 255 come out as ViewOwl's light grey / dark grey / black */
#pragma once
#include "gfx.h"

#define PAPER_STRIDE ((GFX_W + 3) / 4)      // bytes a row, 4 pixels a byte

/* one strip of 0x00RRGGBB light -> fb, which points at the first byte of the whole
 * frame. row cv->y0 + r goes to fb + (cv->y0 + r) * PAPER_STRIDE.
 * leftmost pixel in the top 2 bits. code 3 = white, 2 = light grey, 1 = dark grey,
 * 0 = black, the same as ViewOwl's mono4gray frames */
void paper_pack(const canvas_t *cv, uint8_t *fb);

// how the panel really shows the 4 codes, 0..255 (measured by ViewOwl), for the preview
extern const uint8_t PAPER_LEVEL[4];

/* the two greys of the panel. drawing with exactly these lights gives a clean flat grey,
 * anything in between gets dithered. the 4.2" pulls its greys towards black and white */
#if RADAR_DISPLAY == DISPLAY_EPD_400x300
#define PAPER_DARK  50
#define PAPER_LIGHT 220
#else
#define PAPER_DARK  60
#define PAPER_LIGHT 140
#endif
#define INK_GD  RGB(255 - PAPER_DARK, 255 - PAPER_DARK, 255 - PAPER_DARK)      // ViewOwl's GD, dark grey
#define INK_GL  RGB(255 - PAPER_LIGHT, 255 - PAPER_LIGHT, 255 - PAPER_LIGHT)   // ViewOwl's GL, light grey
#define INK_BK  RGB(255, 255, 255)                                             // black ink
