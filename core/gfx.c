// drawing primitives, everything clips to the current strip (see gfx.h)
#include "gfx.h"
#include <math.h>

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

// clip row range to the strip, returns 0 if nothing left
static int clip_rows(const canvas_t *cv, int *ya, int *yb)
{
    if (*ya < cv->y0) *ya = cv->y0;
    if (*yb > cv->y0 + cv->h - 1) *yb = cv->y0 + cv->h - 1;
    return *ya <= *yb;
}

static inline void plot(uint32_t *row, int x, uint32_t c, int a)
{
    if (a >= 256) row[x] = c;
    else if (a > 0) row[x] = gfx_mix(row[x], c, (uint32_t)a);
}

CORE_HOT void gfx_fill(canvas_t *cv, int x, int y, int w, int h, uint32_t c, int a)
{
    int x0 = imax(x, 0), x1 = imin(x + w, GFX_W) - 1;
    int ya = y, yb = y + h - 1;
    if (x0 > x1 || !clip_rows(cv, &ya, &yb)) return;
    for (int yy = ya; yy <= yb; yy++) {
        uint32_t *row = gfx_row(cv, yy);
        for (int xx = x0; xx <= x1; xx++) plot(row, xx, c, a);
    }
}

CORE_HOT void gfx_hline(canvas_t *cv, int x0, int x1, int y, uint32_t c, int a)
{
    uint32_t *row = gfx_row(cv, y);
    if (!row) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    x0 = imax(x0, 0); x1 = imin(x1, GFX_W - 1);
    for (int x = x0; x <= x1; x++) plot(row, x, c, a);
}

CORE_HOT void gfx_vline(canvas_t *cv, int x, int y0, int y1, uint32_t c, int a)
{
    if (x < 0 || x >= GFX_W) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (!clip_rows(cv, &y0, &y1)) return;
    for (int y = y0; y <= y1; y++) plot(gfx_row(cv, y), x, c, a);
}

// diagonal stripes, the "caution tape" bars like on the reference HUDs
CORE_HOT void gfx_hatch(canvas_t *cv, int x, int y, int w, int h, uint32_t c, int a)
{
    int x0 = imax(x, 0), x1 = imin(x + w, GFX_W) - 1;
    int ya = y, yb = y + h - 1;
    if (x0 > x1 || !clip_rows(cv, &ya, &yb)) return;
    for (int yy = ya; yy <= yb; yy++) {
        uint32_t *row = gfx_row(cv, yy);
        for (int xx = x0; xx <= x1; xx++)
            if (((xx + yy) & 3) < 2) plot(row, xx, c, a);
    }
}

/* AA line as a capsule: coverage of a pixel = its distance to the segment.
 * each row only walks the span the line can reach, else long steep lines
 * would cost the whole bounding box */
CORE_HOT void gfx_line(canvas_t *cv, float x0, float y0, float x1, float y1, float w, uint32_t c, int a)
{
    float hw = w * 0.5f;
    if (w < 1.0f) { a = (int)(a * w); hw = 0.5f; }   // thinner than 1px: draw 1px but fainter
    if (a <= 0) return;
    float reach = hw + 1.0f;
    int ya = (int)floorf(fminf(y0, y1) - reach), yb = (int)ceilf(fmaxf(y0, y1) + reach);
    if (!clip_rows(cv, &ya, &yb)) return;
    float minx = fminf(x0, x1) - reach, maxx = fmaxf(x0, x1) + reach;
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    float inv = len2 > 1e-6f ? 1.0f / len2 : 0.0f;
    float ady = fabsf(dy), len = gfx_sqrt(len2);
    for (int y = ya; y <= yb; y++) {
        float sa = minx, sb = maxx;
        if (ady > 0.5f && ady * 2.0f > fabsf(dx)) {
            // steep: line crosses this row near one x, only look around it
            float t = ((float)y - y0) / dy;
            float xc = x0 + dx * t;
            float half = reach * len / ady + 1.0f;
            sa = fmaxf(minx, xc - half);
            sb = fminf(maxx, xc + half);
        }
        int xa = imax((int)floorf(sa), 0), xb = imin((int)ceilf(sb), GFX_W - 1);
        uint32_t *row = gfx_row(cv, y);
        for (int x = xa; x <= xb; x++) {
            float px = (float)x - x0, py = (float)y - y0;
            float t = (px * dx + py * dy) * inv;
            if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
            float ex = px - t * dx, ey = py - t * dy;
            float cov = hw + 0.5f - gfx_sqrt(ex * ex + ey * ey);
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            plot(row, x, c, (int)(a * cov));
        }
    }
}

CORE_HOT void gfx_ring(canvas_t *cv, float cx, float cy, float r, float w, uint32_t c, int a)
{
    float hw = w * 0.5f;
    if (w < 1.0f) { a = (int)(a * w); hw = 0.5f; }
    float ro = r + hw + 1.0f, ri = r - hw - 1.0f;
    int ya = (int)floorf(cy - ro), yb = (int)ceilf(cy + ro);
    if (!clip_rows(cv, &ya, &yb)) return;
    for (int y = ya; y <= yb; y++) {
        float dy = (float)y - cy;
        if (fabsf(dy) > ro) continue;
        float xo = gfx_sqrt(ro * ro - dy * dy);
        float xi = (ri > 0.0f && fabsf(dy) < ri) ? gfx_sqrt(ri * ri - dy * dy) : -1.0f;
        int xa = imax((int)floorf(cx - xo), 0), xb = imin((int)ceilf(cx + xo), GFX_W - 1);
        uint32_t *row = gfx_row(cv, y);
        for (int x = xa; x <= xb; x++) {
            float dx = (float)x - cx;
            if (xi > 0.0f && fabsf(dx) < xi - 1.0f) {     // hack: jump over the hole inside
                x = (int)floorf(cx + xi - 1.0f);
                continue;
            }
            float d = gfx_sqrt(dx * dx + dy * dy);
            float cov = hw + 0.5f - fabsf(d - r);
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            plot(row, x, c, (int)(a * cov));
        }
    }
}

CORE_HOT void gfx_disc(canvas_t *cv, float cx, float cy, float r, uint32_t c, int a)
{
    float ro = r + 1.0f;
    int ya = (int)floorf(cy - ro), yb = (int)ceilf(cy + ro);
    if (!clip_rows(cv, &ya, &yb)) return;
    for (int y = ya; y <= yb; y++) {
        float dy = (float)y - cy;
        int xa = imax((int)floorf(cx - ro), 0), xb = imin((int)ceilf(cx + ro), GFX_W - 1);
        uint32_t *row = gfx_row(cv, y);
        for (int x = xa; x <= xb; x++) {
            float dx = (float)x - cx;
            float cov = r + 0.5f - gfx_sqrt(dx * dx + dy * dy);
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            plot(row, x, c, (int)(a * cov));
        }
    }
}

// soft additive glow, (1 - d^2/r^2)^2 so no sqrt needed
CORE_HOT void gfx_glow(canvas_t *cv, float cx, float cy, float r, uint32_t c, int a)
{
    if (a <= 0 || r <= 0.5f) return;
    int ya = (int)floorf(cy - r), yb = (int)ceilf(cy + r);
    if (!clip_rows(cv, &ya, &yb)) return;
    float inv = 1.0f / (r * r);
    for (int y = ya; y <= yb; y++) {
        float dy = (float)y - cy;
        int xa = imax((int)floorf(cx - r), 0), xb = imin((int)ceilf(cx + r), GFX_W - 1);
        uint32_t *row = gfx_row(cv, y);
        for (int x = xa; x <= xb; x++) {
            float dx = (float)x - cx;
            float f = 1.0f - (dx * dx + dy * dy) * inv;
            if (f <= 0.0f) continue;
            f *= f;
            int k = (int)(a * f);
            if (k > 0) row[x] = gfx_add(row[x], gfx_scale(c, (uint32_t)(k > 256 ? 256 : k)));
        }
    }
}

CORE_HOT void gfx_tri(canvas_t *cv, float x0, float y0, float x1, float y1, float x2, float y2,
             uint32_t c, int a)
{
    float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (fabsf(area) < 1e-3f) return;
    if (area < 0.0f) { float t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    // edge i: inside when n.p + d >= 0. n is normalized so the value is in pixels
    float ex[3] = { x0, x1, x2 }, ey[3] = { y0, y1, y2 };
    float nx[3], ny[3], nd[3];
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        float vx = ex[j] - ex[i], vy = ey[j] - ey[i];
        float l = gfx_sqrt(vx * vx + vy * vy);
        nx[i] = -vy / l; ny[i] = vx / l;
        nd[i] = -(nx[i] * ex[i] + ny[i] * ey[i]);
    }
    int ya = (int)floorf(fminf(y0, fminf(y1, y2))) - 1, yb = (int)ceilf(fmaxf(y0, fmaxf(y1, y2))) + 1;
    if (!clip_rows(cv, &ya, &yb)) return;
    int xa = imax((int)floorf(fminf(x0, fminf(x1, x2))) - 1, 0);
    int xb = imin((int)ceilf(fmaxf(x0, fmaxf(x1, x2))) + 1, GFX_W - 1);
    for (int y = ya; y <= yb; y++) {
        uint32_t *row = gfx_row(cv, y);
        for (int x = xa; x <= xb; x++) {
            float m = 1e9f;
            for (int i = 0; i < 3; i++) {
                float e = nx[i] * (float)x + ny[i] * (float)y + nd[i];
                if (e < m) m = e;
            }
            float cov = m + 0.5f;
            if (cov <= 0.0f) continue;
            if (cov > 1.0f) cov = 1.0f;
            plot(row, x, c, (int)(a * cov));
        }
    }
}

void gfx_arc(canvas_t *cv, float cx, float cy, float r, float deg0, float deg1, float w,
             uint32_t c, int a)
{
    if (!gfx_touches(cv, (int)(cy - r - w - 1), (int)(cy + r + w + 1))) return;
    float span = deg1 - deg0;
    int n = (int)ceilf(fabsf(span) / 4.0f);   // one segment per 4 degrees
    if (n < 1) n = 1;
    float k = 3.14159265f / 180.0f;
    float px = cx + r * sinf(deg0 * k), py = cy - r * cosf(deg0 * k);
    for (int i = 1; i <= n; i++) {
        float d = (deg0 + span * (float)i / (float)n) * k;
        float qx = cx + r * sinf(d), qy = cy - r * cosf(d);
        gfx_line(cv, px, py, qx, qy, w, c, a);
        px = qx; py = qy;
    }
}

/* 4x4 ordered (bayer) dither 888 -> 565, then byte swap to big endian
 * so DMA can send the buffer as is */
CORE_HOT void gfx_pack565(const canvas_t *cv, uint16_t *out)
{
    /* NOTE: tried three lookup tables instead of this math, it was slower on the
     * board (10.8 ms a frame vs 9.7). dont go back to tables */
    static const CORE_DATA uint8_t bayer[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
    };
    for (int r = 0; r < cv->h; r++) {
        const uint32_t *row = cv->px + r * GFX_W;
        const uint8_t *bm = bayer[(cv->y0 + r) & 3];
        uint16_t *o = out + r * GFX_W;
        for (int x = 0; x < GFX_W; x++) {
            uint32_t p = row[x];
            unsigned d = bm[x & 3];
            unsigned rr = (((p >> 16) & 255) + (d >> 1)) >> 3;
            unsigned gg = (((p >> 8) & 255) + (d >> 2)) >> 2;
            unsigned bb = ((p & 255) + (d >> 1)) >> 3;
            if (rr > 31) rr = 31;
            if (gg > 63) gg = 63;
            if (bb > 31) bb = 31;
            uint16_t v = (uint16_t)((rr << 11) | (gg << 5) | bb);
            o[x] = (uint16_t)((v >> 8) | (v << 8));
        }
    }
}
