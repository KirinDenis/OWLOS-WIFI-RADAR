/* e-paper main screen, made like ViewOwl's plates: white sheet, black frame, light grey
 * header band, cards with a dark grey shadow, bold type (DejaVu Sans Bold).
 *
 * rules that came from the real panel:
 *  - big and crisp. text is ink or paper, no grey edges (font.c), names 20 px, numbers 34 px
 *  - few things, each one readable from a metre away: six networks, not a table of twenty
 *  - grey only where nothing ever changes (bands, shadows, the radar's ring). everything
 *    that changes is black on white, because changes are drawn on top with a partial
 *    refresh, which only knows black and white (epd.c). the greys get repainted by a full
 *    refresh now and then
 *  - the radar has no labels, its dots carry the number of their card. a legend reads
 *    better than words squeezed between dots
 * colours are "light" like everywhere in the ui, paper.c prints the negative:
 * INK_BK = black, INK_GD / INK_GL = the panel's dark and light grey, 0 = white paper */
#include "ui_int.h"
#include "paper.h"

#if RADAR_EPAPER

#define WIDE  (GFX_W >= 2 * GFX_H)       // 792x272. else 400x300
#define CARD_COLS (WIDE ? 2 : 1)
#define CARD_ROWS 3
#define CARDS (CARD_COLS * CARD_ROWS)          // as many numbers on the radar as there are cards
#define CARD_H 66
#define CARD_GAP 8

static void box(canvas_t *cv, int x, int y, int w, int h, uint32_t c)
{
    gfx_hline(cv, x, x + w - 1, y, c, 255);
    gfx_hline(cv, x, x + w - 1, y + h - 1, c, 255);
    gfx_vline(cv, x, y, y + h - 1, c, 255);
    gfx_vline(cv, x + w - 1, y, y + h - 1, c, 255);
}

static int text_r(canvas_t *cv, const font_t *f, int right, int y, const char *s, uint32_t c)
{
    int w = font_width(f, s);
    font_text(cv, f, right - w, y, s, c, 255);
    return w;
}

static void text_c(canvas_t *cv, const font_t *f, int cx, int y, const char *s, uint32_t c)
{
    font_text(cv, f, cx - font_width(f, s) / 2, y, s, c, 255);
}

// networks in card order: the target first, then the loudest
static int card_order(const ui_t *u, int *idx)
{
    int n = 0;
    if (u->target >= 0) idx[n++] = u->target;
    for (int j = 0; j < u->n && n < CARDS; j++)
        if (u->order[j] != u->target) idx[n++] = u->order[j];
    return n;
}

/* every paper screen: double black frame, light grey band with the screen's name, and a
 * white box on the right for what changes (pill, or the network count). the turn takes
 * the box over while it runs */
static void header(ui_t *u, canvas_t *cv, const char *title, const char *pill)
{
    const app_t *a = &u->snap;
    char buf[48];
    box(cv, 2, 2, GFX_W - 4, GFX_H - 4, INK_BK);
    box(cv, 3, 3, GFX_W - 6, GFX_H - 6, INK_BK);
    if (!gfx_touches(cv, 5, PAPER_HEAD)) return;
    gfx_fill(cv, 5, 5, GFX_W - 10, PAPER_HEAD - 9, INK_GL, 255);
    font_text(cv, &FONT_PAPER_L, 14, 12, title, INK_BK, 255);
    gfx_hline(cv, 5, GFX_W - 6, PAPER_HEAD - 4, INK_BK, 255);
    // what changes sits in a white box, so the grey band never has to change
    if (a->sweep == SWEEP_RUN)
        snprintf(buf, sizeof buf, "TURN RIGHT  %u S", (unsigned)((SWEEP_RUN_MS - (u->now - a->sweep_t0) + 999u) / 1000u));
    else if (a->sweep == SWEEP_COUNTDOWN)
        snprintf(buf, sizeof buf, "FACE FORWARD");
    else if (pill)
        snprintf(buf, sizeof buf, "%s", pill);
    else
        snprintf(buf, sizeof buf, "%d NETWORKS", a->n);
    const int pw = WIDE ? 250 : 190, px = GFX_W - 12 - pw;
    gfx_fill(cv, px, 9, pw, PAPER_HEAD - 17, 0, 255);
    box(cv, px, 9, pw, PAPER_HEAD - 17, INK_BK);
    text_c(cv, &FONT_PAPER_L, px + pw / 2, 12, buf, INK_BK);
}

// number badge: black disc with a white number, or hollow when the direction is unknown
static void badge(canvas_t *cv, float x, float y, float r, int num, int hollow)
{
    char buf[4];
    snprintf(buf, sizeof buf, "%d", num);
    if (hollow) {
        gfx_disc(cv, x, y, r, 0, 255);
        gfx_ring(cv, x, y, r - 1.0f, 2.0f, INK_BK, 255);
    } else {
        gfx_disc(cv, x, y, r, INK_BK, 255);
    }
    const font_t *f = r >= 11.0f ? &FONT_PAPER_M : &FONT_PAPER_S;
    text_c(cv, f, (int)x, (int)y - f->line_h / 2, buf, hollow ? INK_BK : 0);
}

/* badges must not cover each other. two routers in one box sit on the same spot, a network
 * a metre away sits under "you" on the map. each badge in turn is pushed out of the ones
 * before it, the first `fixed` points never move. it stays inside the area: a circle when
 * r > 0, else the rectangle */
typedef struct { float cx, cy, r; int x0, y0, x1, y1; } area_t;

static void spread_badges(float *bx, float *by, int n, int fixed, float r, area_t in)
{
    for (int k = fixed; k < n; k++)
        for (int pass = 0; pass < 8; pass++) {
            int moved = 0;
            for (int q = 0; q < k; q++) {
                float dx = bx[k] - bx[q], dy = by[k] - by[q], d = sqrtf(dx * dx + dy * dy);
                if (d >= 2 * r + 2) continue;
                if (d < 0.5f) { dx = cosf(k * 1.1f); dy = sinf(k * 1.1f); d = 1.0f; }   // same spot: pick a side
                bx[k] += dx / d * (2 * r + 2 - d);
                by[k] += dy / d * (2 * r + 2 - d);
                moved = 1;
            }
            if (in.r > 0) {
                float ex = bx[k] - in.cx, ey = by[k] - in.cy, e = sqrtf(ex * ex + ey * ey);
                if (e > in.r) { bx[k] = in.cx + ex / e * in.r; by[k] = in.cy + ey / e * in.r; }
            } else {
                bx[k] = clampf(bx[k], (float)in.x0, (float)in.x1);
                by[k] = clampf(by[k], (float)in.y0, (float)in.y1);
            }
            if (!moved) break;
        }
}

/* the radar, drawn for paper: light grey ring band with the bearings (never changes),
 * white inside with dashed dBm rings, dots with card numbers */
static void radar(ui_t *u, canvas_t *cv)
{
    const rgeo_t *g = &G_MAIN;
    const float cx = (float)g->cx, cy = (float)g->cy, ro = (float)g->r_out, rp = (float)g->r_plot;
    if (!gfx_touches(cv, g->cy - g->r_out - 2, g->cy + g->r_out + 2)) return;
    gfx_disc(cv, cx, cy, ro, INK_GL, 255);
    gfx_disc(cv, cx, cy, rp, 0, 255);
    gfx_ring(cv, cx, cy, ro, 1.6f, INK_BK, 255);
    gfx_ring(cv, cx, cy, rp, 1.2f, INK_BK, 255);
    static const char *deg[4] = { "0", "90", "180", "270" };
    const float rl = (ro + rp) / 2.0f;
    for (int k = 0; k < 12; k++) {                      // ticks every 30, numbers every 90
        float th = k * 30.0f * D2R, s = sinf(th), c = -cosf(th);
        if (k % 3) gfx_line(cv, cx + s * (rp + 2), cy + c * (rp + 2), cx + s * (ro - 2), cy + c * (ro - 2), 1.4f, INK_BK, 255);
        else text_c(cv, &FONT_PAPER_S, (int)(cx + s * rl), (int)(cy + c * rl) - 5, deg[k / 3], INK_BK);
    }
    for (int k = 0; k < 3; k++) {                       // -55 -70 -85, dashed
        float r = rssi_frac(-55.0f - 15.0f * k) * rp;
        for (int a = 0; a < 360; a += 10) gfx_arc(cv, cx, cy, r, (float)a, a + 4.0f, 1.0f, INK_BK, 255);
    }
    for (float d = -rp + 4; d < rp - 4; d += 6) {      // dotted cross
        gfx_disc(cv, cx + d, cy, 0.7f, INK_BK, 255);
        gfx_disc(cv, cx, cy + d, 0.7f, INK_BK, 255);
    }
    gfx_tri(cv, cx - 5, cy - ro - 1, cx + 5, cy - ro - 1, cx, cy - ro + 7, INK_BK, 255);   // ahead
    if (u->snap.sweep == SWEEP_RUN) {                   // the beam, only while turning
        float th = u->beam * D2R;
        gfx_line(cv, cx, cy, cx + sinf(th) * rp, cy - cosf(th) * rp, 2.0f, INK_BK, 255);
    }

    int idx[CARDS], nc = card_order(u, idx);
    for (int j = u->n - 1; j >= 0; j--) {               // the rest: small, quietest first
        int i = u->order[j], numbered = 0;
        for (int k = 0; k < nc; k++) numbered |= idx[k] == i;
        if (numbered) continue;
        const blip_t *b = &u->b[i];
        if (b->ghost) gfx_ring(cv, b->x, b->y, 2.6f, 1.2f, INK_BK, 255);
        else gfx_disc(cv, b->x, b->y, 2.6f, INK_BK, 255);
    }
    /* the ones with cards get their number on the dot. two routers in one box sit on the
     * same spot, so a badge that would cover another one moves aside and a thin line goes
     * back to where it really is */
    const float r = WIDE ? 11.0f : 9.0f;
    float bx[CARDS], by[CARDS];
    for (int k = 0; k < nc; k++) { bx[k] = u->b[idx[k]].x; by[k] = u->b[idx[k]].y; }
    spread_badges(bx, by, nc, 0, r, (area_t){ cx, cy, rp - r, 0, 0, 0, 0 });
    for (int k = nc - 1; k >= 0; k--) {
        const blip_t *b = &u->b[idx[k]];
        if (fabsf(bx[k] - b->x) + fabsf(by[k] - b->y) > 3.0f) {
            gfx_line(cv, b->x, b->y, bx[k], by[k], 1.2f, INK_BK, 255);
            gfx_disc(cv, b->x, b->y, 2.6f, INK_BK, 255);
        }
        if (idx[k] == u->target) gfx_ring(cv, bx[k], by[k], r + 4.0f, 2.0f, INK_BK, 255);
        badge(cv, bx[k], by[k], r, k + 1, b->ghost);
    }
}

static void net_card(ui_t *u, canvas_t *cv, int i, int num, int x, int y, int w)
{
    const contact_t *c = &u->snap.c[i];
    const int h = CARD_H;
    char buf[48];
    if (!gfx_touches(cv, y, y + h + 3)) return;
    gfx_fill(cv, x + 3, y + 3, w, h, INK_GD, 255);      // shadow
    gfx_fill(cv, x, y, w, h, 0, 255);
    box(cv, x, y, w, h, INK_BK);
    if (i == u->target) { box(cv, x + 1, y + 1, w - 2, h - 2, INK_BK); box(cv, x + 2, y + 2, w - 4, h - 4, INK_BK); }
    badge(cv, x + 18.0f, y + 19.0f, 12.0f, num, u->b[i].ghost);

    snprintf(buf, sizeof buf, "%d", (int)lroundf(c->rssi));
    int dw = text_r(cv, &FONT_PAPER_XL, x + w - 8, y + h - 37, buf, INK_BK);
    float tr = app_trend(c), ax = (float)(x + w - 18 - dw), ay = y + h - 20.0f;
    if (tr > TREND_DB) gfx_tri(cv, ax - 6, ay + 4, ax + 6, ay + 4, ax, ay - 6, INK_BK, 255);
    else if (tr < -TREND_DB) gfx_tri(cv, ax - 6, ay - 4, ax + 6, ay - 4, ax, ay + 6, INK_BK, 255);

    font_text_clip(cv, &FONT_PAPER_L, x + 36, y + 7, net_name(c), INK_BK, 255, w - 44);
    int tw = w - 36 - dw - 32;
    snprintf(buf, sizeof buf, "CH %d  %s", c->channel, sec_name(c->sec));
    font_text_clip(cv, WIDE ? &FONT_PAPER_M : &FONT_PAPER_S, x + 36, y + 32, buf, INK_BK, 255, tw);
    float f = clampf((c->rssi + 95.0f) / 65.0f, 0, 1);   // level bar, black on white
    box(cv, x + 36, y + h - 13, tw, 7, INK_BK);
    gfx_fill(cv, x + 37, y + h - 12, (int)((tw - 2) * f), 5, INK_BK, 255);
}

void paper_main(ui_t *u, canvas_t *cv)
{
    header(u, cv, RADAR_NAME, 0);
    radar(u, cv);
    ui_lap(u, 2);
    int idx[CARDS], nc = card_order(u, idx);
    const int x0 = UI_PX, y0 = PAPER_HEAD + 8;
    const int cw = (GFX_W - 14 - x0 - (CARD_COLS - 1) * 12) / CARD_COLS;
    for (int k = 0; k < nc && k < CARD_COLS * CARD_ROWS; k++)
        net_card(u, cv, idx[k], k + 1, x0 + (k % CARD_COLS) * (cw + 12), y0 + (k / CARD_COLS) * (CARD_H + CARD_GAP), cw);
    if (!nc && gfx_touches(cv, y0, y0 + 40)) font_text(cv, &FONT_PAPER_L, x0, y0 + 8, "LISTENING...", INK_BK, 255);
    if (!WIDE && gfx_touches(cv, GFX_H - 70, GFX_H)) {
        // 4.2": under the radar, what the dots mean and what "up" is
        gfx_disc(cv, 22, GFX_H - 58, 4.0f, INK_BK, 255);
        font_text(cv, &FONT_PAPER_S, 34, GFX_H - 64, "direction known", INK_BK, 255);
        gfx_ring(cv, 22, GFX_H - 40, 3.5f, 1.4f, INK_BK, 255);
        font_text(cv, &FONT_PAPER_S, 34, GFX_H - 46, "not yet", INK_BK, 255);
        font_text_clip(cv, &FONT_PAPER_S, 16, GFX_H - 26, u->up_note, INK_BK, 255, UI_PX - 24);
    }
}

/* ---- the other paper screens. MENU goes main, target, list, channels, map ---- */

#define TOP (PAPER_HEAD + 6)             // first row under the band
#define BOT (GFX_H - 8)                  // last row above the frame

static void dashed_v(canvas_t *cv, int x, int y0, int y1, uint32_t c)
{
    for (int y = y0; y < y1; y += 6) gfx_vline(cv, x, y, y + 2 < y1 ? y + 2 : y1, c, 255);
}

/* --- halftones, like the fills under ViewOwl's graphs. a grey the panel doesn't have is
 * made of its two nearest greys in an 8x8 bayer pattern, right here at draw time, so
 * paper.c only ever sees the panel's own levels (it rounds, it doesn't dither: text and
 * lines stay clean, only the fills get the pattern) */
static const uint8_t BAYER8[8][8] = {
    {  0, 32,  8, 40,  2, 34, 10, 42 }, { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44,  4, 36, 14, 46,  6, 38 }, { 60, 28, 52, 20, 62, 30, 54, 22 },
    {  3, 35, 11, 43,  1, 33,  9, 41 }, { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47,  7, 39, 13, 45,  5, 37 }, { 63, 31, 55, 23, 61, 29, 53, 21 },
};

// paper brightness p (0 black .. 255 white) at x,y -> the light value of one panel grey
static uint32_t halftone(int x, int y, int p)
{
    static const int lv[4] = { 0, PAPER_DARK, PAPER_LIGHT, 255 };
    int k = 0;
    while (k < 2 && p > lv[k + 1]) k++;
    int f = (p - lv[k]) * 64 / (lv[k + 1] - lv[k]);
    int l = f > BAYER8[y & 7][x & 7] ? lv[k + 1] : lv[k];
    return RGB(255 - l, 255 - l, 255 - l);
}

// ViewOwl's gradient under a curve: light grey at the top of the box fading to white at its bottom
static void shade_column(canvas_t *cv, int x, int ytop, int gy, int gh)
{
    for (int y = ytop < gy + 1 ? gy + 1 : ytop; y < gy + gh - 1; y++) {
        uint32_t *row = gfx_row(cv, y);
        if (row) row[x] = halftone(x, y, PAPER_LIGHT + (255 - PAPER_LIGHT) * (y - gy) / gh);
    }
}

/* a dB scale that fits what is there. with a fixed -30..-95 everything real (-70..-92)
 * lay on the bottom. 10 dB steps, so it doesn't jump with every frame */
static void db_range(float lo, float hi, int min_span, int *top, int *bot)
{
    *top = (int)ceilf((hi + 4.0f) / 10.0f) * 10;
    *bot = (int)floorf((lo - 4.0f) / 10.0f) * 10;
    if (*top > -20) *top = -20;
    if (*bot < -100) *bot = -100;
    while (*top - *bot < min_span) { if (*bot > -100) *bot -= 10; else *top += 10; }
}

// level over the last minute, scaled to what it did, shaded underneath
static void history(canvas_t *cv, const contact_t *c, int x, int y, int w, int h)
{
    if (!gfx_touches(cv, y - 14, y + h + 2)) return;
    char buf[48];
    float lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < HIST_LEN; k++)
        if (c->hist[k] != HIST_NONE) { lo = fminf(lo, c->hist[k]); hi = fmaxf(hi, c->hist[k]); }
    if (hi < lo) lo = hi = c->rssi;
    int top, bot;
    db_range(lo, hi, 20, &top, &bot);
#define HY(db) (y + 1 + ((float)top - (db)) * (float)(h - 3) / (float)(top - bot))
    float vx[HIST_LEN], vy[HIST_LEN];
    int ok[HIST_LEN];
    for (int i = 0; i < HIST_LEN; i++) {
        int k = (c->hist_head + i) % HIST_LEN;
        ok[i] = c->hist[k] != HIST_NONE;
        vx[i] = x + 2 + (float)i * (float)(w - 5) / (HIST_LEN - 1);
        vy[i] = ok[i] ? HY((float)c->hist[k]) : 0;
    }
    for (int i = 1; i < HIST_LEN; i++) {                 // shade under the curve first
        if (!ok[i] || !ok[i - 1]) continue;
        for (int xx = (int)vx[i - 1]; xx < (int)vx[i]; xx++) {
            float t = (xx - vx[i - 1]) / (vx[i] - vx[i - 1]);
            shade_column(cv, xx, (int)(vy[i - 1] + (vy[i] - vy[i - 1]) * t) + 1, y, h);
        }
    }
    for (int db = top - 10; db > bot; db -= 10) gfx_hline(cv, x + 1, x + w - 2, (int)HY((float)db), INK_GL, 255);
    box(cv, x, y, w, h, INK_BK);
    float px = 0, py = 0;
    int prev = 0;
    for (int i = 0; i < HIST_LEN; i++) {
        if (!ok[i]) { prev = 0; continue; }
        if (prev) gfx_line(cv, px, py, vx[i], vy[i], 2.0f, INK_BK, 255);
        px = vx[i]; py = vy[i]; prev = 1;
    }
    if (prev) gfx_disc(cv, px, py, 3.5f, INK_BK, 255);
    snprintf(buf, sizeof buf, "LAST MINUTE  %d..%d dBm", bot, top);
    font_text(cv, &FONT_PAPER_S, x, y - 13, buf, INK_GD, 255);
#undef HY
}

// closeness as segments: black when lit, just the outline when not
static void closeness(canvas_t *cv, float f, int x, int y, int w, int h)
{
    if (!gfx_touches(cv, y - 14, y + h + 14)) return;
    const int segs = 10, sw = (w - (segs - 1) * 4) / segs;
    for (int s = 0; s < segs; s++) {
        int sx = x + s * (sw + 4);
        if ((float)s < f * segs) gfx_fill(cv, sx, y, sw, h, INK_BK, 255);
        else box(cv, sx, y, sw, h, INK_BK);
    }
    font_text(cv, &FONT_PAPER_S, x, y + h + 2, "FAR", INK_GD, 255);
    text_r(cv, &FONT_PAPER_S, x + w, y + h + 2, "NEAR", INK_GD);
}

// which way, relative to where you face: ring and "ahead" mark never change, the arrow does
static void dial(ui_t *u, canvas_t *cv, int cx, int cy, int r)
{
    if (!gfx_touches(cv, cy - r - 30, cy + r + 30)) return;
    const blip_t *b = &u->b[u->target];
    char buf[32];
    gfx_disc(cv, (float)cx, (float)cy, (float)r, INK_GL, 255);
    gfx_disc(cv, (float)cx, (float)cy, r - 8.0f, 0, 255);
    gfx_ring(cv, (float)cx, (float)cy, (float)r, 1.5f, INK_BK, 255);
    gfx_ring(cv, (float)cx, (float)cy, r - 8.0f, 1.0f, INK_BK, 255);
    gfx_tri(cv, cx - 6.0f, (float)(cy - r - 2), cx + 6.0f, (float)(cy - r - 2), (float)cx, (float)(cy - r + 8), INK_BK, 255);
    if (b->src) {
        float th = b->ang * D2R, s = sinf(th), c = cosf(th), L = r - 14.0f;
        float tx = cx + s * L, ty = cy - c * L;
        gfx_line(cv, (float)cx, (float)cy, tx - s * 8, ty + c * 8, 4.0f, INK_BK, 255);
        gfx_tri(cv, tx, ty, tx - s * 14 - c * 8, ty + c * 14 - s * 8, tx - s * 14 + c * 8, ty + c * 14 + s * 8, INK_BK, 255);
        gfx_disc(cv, (float)cx, (float)cy, 4.0f, INK_BK, 255);
        snprintf(buf, sizeof buf, "%03d\xC2\xB0 \xC2\xB1%d", (int)lroundf(b->ang) % 360, (int)lroundf(b->sd));
        text_c(cv, &FONT_PAPER_M, cx, cy + r + 4, buf, INK_BK);
    } else {
        text_c(cv, &FONT_PAPER_XL, cx, cy - 18, "?", INK_BK);
        text_c(cv, &FONT_PAPER_S, cx, cy + r + 4, "do a 360 turn", INK_BK);
    }
}

/* the hunt: one network, the number as big as the panel allows */
void paper_target(ui_t *u, canvas_t *cv)
{
    const contact_t *c = &u->snap.c[u->target];
    char buf[64];
    int rank = 0;
    for (int k = 0; k < u->n; k++) if (u->order[k] == u->target) rank = k + 1;
    snprintf(buf, sizeof buf, "#%d OF %d", rank, u->n);
    header(u, cv, "TARGET", buf);

    font_text_clip(cv, &FONT_PAPER_L, 14, TOP, net_name(c), INK_BK, 255, WIDE ? 316 : GFX_W - 28);
    snprintf(buf, sizeof buf, "CH %d \xC2\xB7 %s \xC2\xB7 %02X:%02X:%02X:%02X:%02X:%02X", c->channel, sec_name(c->sec),
             c->bssid[0], c->bssid[1], c->bssid[2], c->bssid[3], c->bssid[4], c->bssid[5]);
    font_text_clip(cv, &FONT_PAPER_S, 14, TOP + 24, buf, INK_BK, 255, WIDE ? 316 : 250);   // 4.2: the dial sits right of it

    snprintf(buf, sizeof buf, "%d", (int)lroundf(c->rssi));
    int w = font_text(cv, &FONT_PAPER_XXL, 8, TOP + 40, buf, INK_BK, 255);
    font_text(cv, &FONT_PAPER_M, 12 + w, TOP + 86, "dBm", INK_BK, 255);

    float tr = app_trend(c);
    const char *word = tr > TREND_DB ? "WARMER" : tr < -TREND_DB ? "COLDER" : "STEADY";
    const int ty = WIDE ? TOP + 112 : TOP + 108;
    float ax = 24, ay = ty + 11.0f;
    if (tr > TREND_DB) gfx_tri(cv, ax - 10, ay + 7, ax + 10, ay + 7, ax, ay - 10, INK_BK, 255);
    else if (tr < -TREND_DB) gfx_tri(cv, ax - 10, ay - 7, ax + 10, ay - 7, ax, ay + 10, INK_BK, 255);
    else gfx_fill(cv, 14, (int)ay - 2, 20, 5, INK_BK, 255);
    font_text(cv, &FONT_PAPER_L, 42, ty, word, INK_BK, 255);
    snprintf(buf, sizeof buf, "%+.1f dB in 10 s", (double)tr);
    font_text(cv, &FONT_PAPER_S, 42, ty + 24, buf, INK_GD, 255);
    float d = app_distance_m(c->rssi);
    if (d < 10.0f) snprintf(buf, sizeof buf, "~%.1f M", (double)d);
    else snprintf(buf, sizeof buf, "~%d M", (int)lroundf(d));
    float f = clampf((c->rssi + 92.0f) / 60.0f, 0, 1);
    if (WIDE) {
        font_text(cv, &FONT_PAPER_L, 200, ty, buf, INK_BK, 255);
        font_text(cv, &FONT_PAPER_S, 200, ty + 24, "rough, from the level", INK_GD, 255);
        closeness(cv, f, 346, TOP + 18, 220, 30);
        history(cv, c, 346, TOP + 82, 220, BOT - TOP - 82);
        gfx_vline(cv, 336, TOP, BOT, INK_GL, 255);
        gfx_vline(cv, 578, TOP, BOT, INK_GL, 255);
        dial(u, cv, 680, TOP + 98, 74);
    } else {
        text_r(cv, &FONT_PAPER_L, GFX_W - 14, ty + 20, buf, INK_BK);    // under the dial's label, not next to it
        dial(u, cv, 330, TOP + 62, 34);
        closeness(cv, f, 14, ty + 52, GFX_W - 28, 18);
        history(cv, c, 14, ty + 104, GFX_W - 28, BOT - ty - 104);
    }
}

/* the list, big rows. as many as fit, in two columns on the wide one. the page that holds
 * the target is shown, the dial walks thru them */
void paper_list(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char buf[64];
    const int cols = WIDE ? 2 : 1, rows = WIDE ? 5 : 6, rh = (BOT - TOP) / rows, per = cols * rows;
    const int cw = (GFX_W - 20 - (cols - 1) * 14) / cols;
    int at = 0;
    for (int k = 0; k < a->n; k++) if (u->list[k] == u->target) at = k;
    int first = (at / per) * per, pages = (a->n + per - 1) / per;
    static const char *sorts[3] = { "BY SIGNAL", "BY NAME", "BY CHANNEL" };
    if (pages > 1) snprintf(buf, sizeof buf, "%d NETS  %d/%d", a->n, first / per + 1, pages);
    else snprintf(buf, sizeof buf, "%d NETWORKS", a->n);
    header(u, cv, sorts[u->sort], buf);
    for (int s = 0; s < per; s++) {
        int col = s / rows, row = s % rows;
        int x = 10 + col * (cw + 14), y = TOP + row * rh;
        if (!gfx_touches(cv, y, y + rh)) continue;
        if (row < rows - 1) gfx_hline(cv, x, x + cw - 1, y + rh - 1, INK_GL, 255);   // never moves, so grey is fine
        int k = first + s;
        if (k >= a->n) continue;
        int i = u->list[k];
        const contact_t *c = &a->c[i];
        if (i == u->target) { box(cv, x - 3, y, cw + 6, rh - 1, INK_BK); box(cv, x - 2, y + 1, cw + 4, rh - 3, INK_BK); }
        snprintf(buf, sizeof buf, "%d", k + 1);
        text_r(cv, &FONT_PAPER_M, x + 24, y + 5, buf, INK_BK);
        snprintf(buf, sizeof buf, "%d", (int)lroundf(c->rssi));
        int dw = text_r(cv, &FONT_PAPER_L, x + cw - 6, y + (rh - 22) / 2, buf, INK_BK);
        font_text_clip(cv, &FONT_PAPER_L, x + 32, y + 2, net_name(c), INK_BK, 255, cw - 32 - dw - 16);
        int n = snprintf(buf, sizeof buf, "CH %d \xC2\xB7 %s \xC2\xB7 ~%d M", c->channel, sec_name(c->sec),
                         (int)lroundf(app_distance_m(c->rssi)));
        if (u->b[i].src) snprintf(buf + n, sizeof buf - (size_t)n, " \xC2\xB7 %03d\xC2\xB0", (int)lroundf(u->b[i].ang) % 360);
        font_text_clip(cv, &FONT_PAPER_S, x + 32, y + 25, buf, INK_BK, 255, cw - 32 - dw - 16);
    }
}

/* channels: one hump a network, the scale fitted to the loudest one, all of them shaded
 * underneath like ViewOwl's graphs, names on the peaks of the loudest. under it which
 * channels are crowded */
static int s_sp_top = -30, s_sp_bot = -95;             // scale of the graph being drawn
static float sp_x(float ch, int gx, int gw) { return gx + ch * (float)gw / 14.0f; }
static float sp_y(float dbm, int gy, int gh) { return gy + 1 + (s_sp_top - dbm) / (float)(s_sp_top - s_sp_bot) * (float)(gh - 2); }

// how high the humps reach at this channel position (dBm), the top of the shading
static float sp_level(const ui_t *u, float ch, int count)
{
    float best = -200.0f;
    for (int j = 0; j < count; j++) {
        const contact_t *c = &u->snap.c[u->order[j]];
        float e = fabsf(ch - c->channel) - 1.4f, lvl = e > 0 ? c->rssi - 40.0f * e * e : c->rssi;
        if (lvl > best) best = lvl;
    }
    return best;
}

void paper_spectrum(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char buf[96];
    header(u, cv, WIDE ? "CHANNELS 2.4 GHZ" : "CHANNELS", 0);
    const int gx = WIDE ? 60 : 44, gy = TOP + 4, gw = GFX_W - 14 - gx, gh = WIDE ? 150 : 170;
    const int count = u->n < 16 ? u->n : 16;
    float lo = 1e9f, hi = -1e9f;
    for (int j = 0; j < count; j++) { float r = a->c[u->order[j]].rssi; lo = fminf(lo, r); hi = fmaxf(hi, r); }
    if (hi < lo) lo = hi = -70.0f;
    db_range(lo - 6.0f, hi, 30, &s_sp_top, &s_sp_bot);
    if (gfx_touches(cv, gy - 4, gy + gh + 20)) {
        for (int x = gx + 1; x < gx + gw - 1; x++) {      // halftone under all the humps
            float lvl = sp_level(u, (float)(x - gx) * 14.0f / (float)gw, count);
            if (lvl > s_sp_bot) shade_column(cv, x, (int)sp_y(lvl, gy, gh) + 1, gy, gh);
        }
        for (int db = s_sp_top - 10; db > s_sp_bot; db -= 10) {   // grid every 10 dB, labelled
            int y = (int)sp_y((float)db, gy, gh);
            gfx_hline(cv, gx + 1, gx + gw - 2, y, INK_GL, 255);
            snprintf(buf, sizeof buf, "%d", db);
            text_r(cv, &FONT_PAPER_S, gx - 6, y - 6, buf, INK_BK);
        }
        for (int ch = 1; ch <= 13; ch++) {
            int x = (int)sp_x((float)ch, gx, gw);
            gfx_vline(cv, x, gy + gh, gy + gh + 4, INK_BK, 255);
            snprintf(buf, sizeof buf, "%d", ch);
            text_c(cv, &FONT_PAPER_S, x, gy + gh + 5, buf, INK_BK);
        }
        if (u->chan_sel > 0) {                            // picked with the dial
            int x = (int)sp_x((float)u->chan_sel, gx, gw), hw = gw / 28;
            dashed_v(cv, x - hw, gy + 1, gy + gh - 1, INK_BK);
            dashed_v(cv, x + hw, gy + 1, gy + gh - 1, INK_BK);
        }
        for (int j = count - 1; j >= 0; j--) {
            const contact_t *c = &a->c[u->order[j]];
            float px = 0, py = 0;
            for (float t = -2.6f; t <= 2.61f; t += 0.1f) {
                float e = fabsf(t) - 1.4f, lvl = e > 0 ? c->rssi - 40.0f * e * e : c->rssi;
                if (lvl < s_sp_bot) lvl = (float)s_sp_bot;
                float x = sp_x(c->channel + t, gx, gw), y = sp_y(lvl, gy, gh);
                if (x < gx + 1 || x > gx + gw - 2) { px = 0; continue; }
                if (px > 0) gfx_line(cv, px, py, x, y, j < 6 ? 2.0f : 1.0f, INK_BK, 255);
                px = x; py = y;
            }
        }
        int lx[6], ly[6], lw[6], nl = 0;                  // names on the loudest peaks, moved up if they overlap
        for (int j = 0; j < u->n && nl < 6; j++) {
            const contact_t *c = &a->c[u->order[j]];
            int w = font_width(&FONT_PAPER_S, net_name(c));
            if (w > 110) w = 110;
            int x = (int)sp_x((float)c->channel, gx, gw) - w / 2, y = (int)sp_y(c->rssi, gy, gh) - 14;
            if (x < gx + 2) x = gx + 2;
            if (x + w > gx + gw - 2) x = gx + gw - 2 - w;
            int ok = 0;
            for (int tries = 0; tries < 5 && !ok; tries++) {
                ok = y >= gy + 2;
                for (int q = 0; q < nl && ok; q++)
                    if (x < lx[q] + lw[q] + 4 && lx[q] < x + w + 4 && y < ly[q] + 13 && ly[q] < y + 13) ok = 0;
                if (!ok) y -= 13;
            }
            if (!ok) continue;
            lx[nl] = x; ly[nl] = y; lw[nl] = w; nl++;
            gfx_fill(cv, x - 2, y, w + 4, 13, 0, 255);
            font_text_clip(cv, &FONT_PAPER_S, x, y, net_name(c), INK_BK, 255, 110);
        }
        box(cv, gx, gy, gw, gh, INK_BK);
    }
    // under the graph: crowded channels, or who sits on the picked one
    const int y = gy + gh + 24;
    if (!gfx_touches(cv, y, BOT)) return;
    int len = 0;
    if (u->chan_sel > 0) {
        len = snprintf(buf, sizeof buf, "CH %d: ", u->chan_sel);
        int n = 0;
        for (int j = 0; j < u->n && len < (int)sizeof buf - 24; j++) {
            const contact_t *c = &a->c[u->order[j]];
            if (c->channel != u->chan_sel) continue;
            len += snprintf(buf + len, sizeof buf - (size_t)len, "%s%s", n++ ? ", " : "", net_name(c));
            if (len >= (int)sizeof buf) { len = (int)sizeof buf - 1; break; }   // the line gets clipped anyway
        }
        if (!n && len < (int)sizeof buf - 8) snprintf(buf + len, sizeof buf - (size_t)len, "nobody");
    } else {
        int cnt[14] = { 0 };
        for (int i = 0; i < a->n; i++) if (a->c[i].channel >= 1 && a->c[i].channel <= 13) cnt[a->c[i].channel]++;
        len = snprintf(buf, sizeof buf, "CROWDED ");
        for (int k = 0; k < 3; k++) {
            int best = 0;
            for (int ch = 1; ch <= 13; ch++) if (cnt[ch] > cnt[best]) best = ch;
            if (!best || !cnt[best]) break;
            len += snprintf(buf + len, sizeof buf - (size_t)len, "  CH %d: %d", best, cnt[best]);
            cnt[best] = -1;
        }
    }
    font_text_clip(cv, &FONT_PAPER_M, gx, y, buf, INK_BK, 255, GFX_W - 14 - gx);
}

/* the walk map: path and networks, numbered like the cards on the main screen, legend
 * with how far each one is. the map moves when the walk grows, so all of it is black */
void paper_map(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    const mapview_t *mv = &a->map;
    char buf[64];
    snprintf(buf, sizeof buf, "%02u:%02u WALKED", (unsigned)(mv->seconds / 60u), (unsigned)(mv->seconds % 60u));
    header(u, cv, "WALK MAP", buf);
    const int mx0 = 10, my0 = TOP, mw = WIDE ? 500 : GFX_W - 20, mh = WIDE ? BOT - TOP : 160;

    // fit: the walk and the networks it trusts within 14 m of the walk
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, cx = 0, cy = 0;
    for (int t = 0; t < mv->T; t++) { cx += mv->px[t]; cy += mv->py[t]; }
    if (mv->T) { cx /= mv->T; cy /= mv->T; }
    for (int i = 0; i < mv->T + mv->n; i++) {
        float x = i < mv->T ? mv->px[i] : mv->ax[i - mv->T], y = i < mv->T ? mv->py[i] : mv->ay[i - mv->T];
        if (i >= mv->T && (!mv->ok[i - mv->T] || hypotf(x - cx, y - cy) > 14.0f)) continue;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
    }
    float s = 20.0f, fmx = 0, fmy = 0;
    if (x1 >= x0) {
        fmx = (x0 + x1) / 2; fmy = (y0 + y1) / 2;
        s = clampf(fminf((mw - 40) / fmaxf(x1 - x0, 2.0f), (mh - 40) / fmaxf(y1 - y0, 2.0f)), 4.0f, 60.0f);
    }
    const float mcx = mx0 + mw / 2.0f, mcy = my0 + mh / 2.0f;
#define SX(x) (mcx + ((x) - fmx) * s)
#define SY(y) (mcy - ((y) - fmy) * s)
    if (gfx_touches(cv, my0, my0 + mh)) {
        float step = s >= 14.0f ? 1.0f : s >= 7.0f ? 2.0f : 5.0f;
        for (float gxm = floorf((fmx - mw / 2 / s) / step) * step; SX(gxm) < mx0 + mw; gxm += step)   // dotted metre grid
            for (int y = my0 + 3; y < my0 + mh - 3; y += 6) if (SX(gxm) > mx0 + 2) gfx_fill(cv, (int)SX(gxm), y, 1, 1, INK_BK, 255);
        for (float gym = floorf((fmy - mh / 2 / s) / step) * step; SY(gym) > my0; gym += step)
            for (int x = mx0 + 3; x < mx0 + mw - 3; x += 6) if (SY(gym) < my0 + mh - 2) gfx_fill(cv, x, (int)SY(gym), 1, 1, INK_BK, 255);
        float px = 0, py = 0;                            // the path, 5 s averaged like on the LCD
        for (int t = 0; t < mv->T; t++) {
            float ax = 0, ay = 0;
            int n = 0;
            for (int q = t - 2; q <= t + 2; q++) if (q >= 0 && q < mv->T) { ax += mv->px[q]; ay += mv->py[q]; n++; }
            if (t == mv->T - 1) { ax = mv->px[t] * n; ay = mv->py[t] * n; }
            float sx = SX(ax / n), sy = SY(ay / n);
            if (t) gfx_line(cv, px, py, sx, sy, t > mv->T / 2 ? 2.2f : 1.2f, INK_BK, 255);
            px = sx; py = sy;
        }
        int idx[CARDS], nc = card_order(u, idx);
        float bx[CARDS + 1], by[CARDS + 1], rx[CARDS + 1], ry[CARDS + 1];
        int bn = 0, bnum[CARDS + 1], bok[CARDS + 1];
        if (mv->T) { bx[0] = SX(mv->px[mv->T - 1]); by[0] = SY(mv->py[mv->T - 1]); bn = 1; }   // you: never moves
        for (int k = 0; k < mv->n; k++) {                // networks: numbered if they have a card
            float sx = SX(mv->ax[k]), sy = SY(mv->ay[k]);
            if (sx < mx0 + 12 || sx > mx0 + mw - 12 || sy < my0 + 12 || sy > my0 + mh - 12) continue;
            int num = 0;
            for (int q = 0; q < nc; q++) if (a->c[idx[q]].id == mv->id[k]) num = q + 1;
            if (num) { rx[bn] = bx[bn] = sx; ry[bn] = by[bn] = sy; bnum[bn] = num; bok[bn] = mv->ok[k]; bn++; }
            else if (mv->ok[k]) gfx_disc(cv, sx, sy, 3.0f, INK_BK, 255);
            else gfx_ring(cv, sx, sy, 3.0f, 1.2f, INK_BK, 255);
        }
        spread_badges(bx, by, bn, mv->T ? 1 : 0, 11.0f, (area_t){ 0, 0, 0, mx0 + 12, my0 + 12, mx0 + mw - 12, my0 + mh - 12 });
        for (int k = mv->T ? 1 : 0; k < bn; k++) {
            if (fabsf(bx[k] - rx[k]) + fabsf(by[k] - ry[k]) > 3.0f) {
                gfx_line(cv, rx[k], ry[k], bx[k], by[k], 1.2f, INK_BK, 255);
                gfx_disc(cv, rx[k], ry[k], 2.6f, INK_BK, 255);
            }
            badge(cv, bx[k], by[k], 11.0f, bnum[k], !bok[k]);
        }
        if (mv->T) {                                     // you
            float sx = SX(mv->px[mv->T - 1]), sy = SY(mv->py[mv->T - 1]);
            gfx_disc(cv, sx, sy, 9.0f, 0, 255);
            if (mv->heading_ok) {
                float h = mv->heading * D2R, hs = sinf(h), hc = cosf(h);
                gfx_tri(cv, sx + 9 * hs, sy - 9 * hc, sx - 6 * hc - 5 * hs, sy - 6 * hs + 5 * hc, sx + 6 * hc - 5 * hs, sy + 6 * hs + 5 * hc, INK_BK, 255);
            } else {
                gfx_disc(cv, sx, sy, 5.0f, INK_BK, 255);
            }
        }
        int bar = (int)(step * s);                       // scale bar
        gfx_fill(cv, mx0 + 10, my0 + mh - 12, bar, 3, INK_BK, 255);
        snprintf(buf, sizeof buf, "%d M", (int)step);
        font_text(cv, &FONT_PAPER_S, mx0 + 16 + bar, my0 + mh - 18, buf, INK_BK, 255);
        if (!mv->active) {                               // not enough walk yet: say what to do, on white
            const int bw = WIDE ? 380 : 330, bx = (int)mcx - bw / 2, by = (int)mcy - 30;
            gfx_fill(cv, bx, by, bw, 60, 0, 255);
            box(cv, bx, by, bw, 60, INK_BK);
            text_c(cv, &FONT_PAPER_L, (int)mcx, by + 8, "WALK SLOWLY THRU EVERY ROOM", INK_BK);
            snprintf(buf, sizeof buf, "the map needs 3 m of walk, now %.1f m", (double)mv->spread);
            text_c(cv, &FONT_PAPER_S, (int)mcx, by + 36, buf, INK_BK);
        }
        box(cv, mx0, my0, mw, mh, INK_BK);
    }
#undef SX
#undef SY

    // legend: how far each numbered one is from you, and what "up" means
    int idx[CARDS], nc = card_order(u, idx);
    const int lx = WIDE ? mx0 + mw + 14 : 14, ly = WIDE ? TOP : my0 + mh + 6;
    const int lw = WIDE ? GFX_W - 14 - lx : GFX_W - 28, lines = WIDE ? nc : (nc < 3 ? nc : 3);
    if (!gfx_touches(cv, ly, BOT)) return;
    font_text_clip(cv, &FONT_PAPER_S, lx, ly, mv->aligned ? "UP = WHERE YOU FACED AT THE TURN" : "UP = FIRST STEPS, LEFT/RIGHT ?", INK_BK, 255, lw);
    for (int q = 0; q < lines; q++) {
        int y = ly + (WIDE ? 18 : 14) + q * (WIDE ? 34 : 20), i = idx[q], k = -1;
        for (int m = 0; m < mv->n; m++) if (mv->id[m] == a->c[i].id) k = m;
        if (WIDE) badge(cv, lx + 12.0f, y + 12.0f, 11.0f, q + 1, k < 0 || !mv->ok[k]);
        else { snprintf(buf, sizeof buf, "%d", q + 1); font_text(cv, &FONT_PAPER_M, lx, y, buf, INK_BK, 255); }
        if (k >= 0 && mv->T) {
            float d = hypotf(mv->ax[k] - mv->px[mv->T - 1], mv->ay[k] - mv->py[mv->T - 1]);
            snprintf(buf, sizeof buf, "%s~%d M", mv->ok[k] ? "" : "? ", (int)lroundf(d));
        } else {
            snprintf(buf, sizeof buf, "not on map");
        }
        int dw = text_r(cv, &FONT_PAPER_M, lx + lw, y + 4, buf, INK_BK);
        font_text_clip(cv, &FONT_PAPER_M, lx + 30, y + 4, net_name(&a->c[i]), INK_BK, 255, lw - 30 - dw - 10);
    }
}

#endif
