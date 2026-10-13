/* gfx - strip renderer for the RGB565 panels (size from config.h).
 * 320x240 frame is 153600 bytes, biggest free block after WiFi is ~110 KB (no PSRAM), so we
 * draw the scene once per strip of rows in 0x00RRGGBB, then dither to 565 and send by DMA.
 * 8 bit channels + dither keeps dark gradients from banding. all primitives clip to the strip */
#pragma once
#include <stdint.h>

/* hot loops go to IRAM on ESP32: a frame runs all this code ten times over
 * and every flash cache miss costs microseconds */
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define CORE_HOT  IRAM_ATTR
#define CORE_DATA DRAM_ATTR
#else
#define CORE_HOT
#define CORE_DATA
#endif

#include "config.h"   // GFX_W, GFX_H come from the chosen display

typedef struct {
    uint32_t *px;   /* GFX_W * h pixels, 0x00RRGGBB, row by row */
    int y0;         /* first screen row of the strip */
    int h;          /* rows in strip */
} canvas_t;

#define RGB(r, g, b) ((uint32_t)((((r) & 255) << 16) | (((g) & 255) << 8) | ((b) & 255)))

// alpha is 0..256 everywhere (256 = opaque) so >> 8 is exact
static inline uint32_t gfx_mix(uint32_t d, uint32_t s, uint32_t a)
{
    uint32_t ia = 256 - a;
    uint32_t rb = ((s & 0xFF00FFu) * a + (d & 0xFF00FFu) * ia) >> 8;
    uint32_t g  = ((s & 0x00FF00u) * a + (d & 0x00FF00u) * ia) >> 8;
    return (rb & 0xFF00FFu) | (g & 0x00FF00u);
}

static inline uint32_t gfx_scale(uint32_t c, uint32_t a)
{
    return ((((c & 0xFF00FFu) * a) >> 8) & 0xFF00FFu) | ((((c & 0x00FF00u) * a) >> 8) & 0x00FF00u);
}

// saturating add per channel, light stops at 255 and never wraps to black
static inline uint32_t gfx_add(uint32_t d, uint32_t s)
{
    /* branchless: add low 7 bits of each channel, put the top bit back with xor,
     * then every channel that carried out gets filled with 0xFF */
    uint32_t t = (d & 0x7F7F7Fu) + (s & 0x7F7F7Fu);
    uint32_t sum = t ^ ((d ^ s) & 0x808080u);
    uint32_t ovf = ((d & s) | ((d | s) & t)) & 0x808080u;
    return sum | ((ovf >> 7) * 0xFFu);
}

/* fast sqrt for anti-aliasing: inverse root guess + one Newton step, ~0.2% off,
 * plenty for AA. ESP32 has no sqrt instruction and AA shapes need one per pixel */
static inline float gfx_sqrt(float x)
{
    if (x <= 0.0f) return 0.0f;
    union { float f; uint32_t i; } u;
    u.f = x;
    u.i = 0x5f3759dfu - (u.i >> 1);
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
}

// pointer to screen row y, or 0 if its not in this strip
static inline uint32_t *gfx_row(const canvas_t *cv, int y)
{
    int r = y - cv->y0;
    return (r >= 0 && r < cv->h) ? cv->px + r * GFX_W : 0;
}

// is any of the rows ya..yb inside this strip?
static inline int gfx_touches(const canvas_t *cv, int ya, int yb)
{
    return yb >= cv->y0 && ya < cv->y0 + cv->h;
}

void gfx_fill(canvas_t *cv, int x, int y, int w, int h, uint32_t c, int a);
void gfx_hline(canvas_t *cv, int x0, int x1, int y, uint32_t c, int a);
void gfx_vline(canvas_t *cv, int x, int y0, int y1, uint32_t c, int a);
void gfx_hatch(canvas_t *cv, int x, int y, int w, int h, uint32_t c, int a);

/* --- anti-aliased, float coords at pixel centres --- */
void gfx_line(canvas_t *cv, float x0, float y0, float x1, float y1, float w, uint32_t c, int a);
void gfx_ring(canvas_t *cv, float cx, float cy, float r, float w, uint32_t c, int a);
void gfx_disc(canvas_t *cv, float cx, float cy, float r, uint32_t c, int a);
void gfx_glow(canvas_t *cv, float cx, float cy, float r, uint32_t c, int a);   /* additive */
void gfx_tri(canvas_t *cv, float x0, float y0, float x1, float y1, float x2, float y2,
             uint32_t c, int a);
// arc as a polyline. degrees, 0 = up, clockwise
void gfx_arc(canvas_t *cv, float cx, float cy, float r, float deg0, float deg1, float w,
             uint32_t c, int a);

// dither strip to RGB565, high byte first like the panel wants
void gfx_pack565(const canvas_t *cv, uint16_t *out);
