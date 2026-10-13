// radar disc, blips and labels. works for both geometries, main screen and full screen
#include "ui_int.h"
#include <stdlib.h>

rgeo_t G_MAIN, G_FULL;   // worked out from the screen size in radar_init
int UI_PX, UI_PW;

static const int DBM_RINGS[4] = { -40, -55, -70, -85 };
/* polar lookup for one quadrant, one byte each: radius in whole px, angle from
 * vertical in 1/256 of 90 deg. two bytes each would cost 31 KB of ram, we dont have that */
static uint8_t  *s_r8, *s_phi8;
static uint8_t  *s_stat;              /* static light of the disc, see build_static */
static const rgeo_t *s_stat_geo;

static float cover(float r, float R, float w) { return clampf(w * 0.5f + 0.5f - fabsf(r - R), 0, 1); }

static void build_geo(rgeo_t *g)
{
    int rq = g->r_out + 5;
    for (int i = 0; i < rq * 16; i++) {
        float r = (float)i / 16.0f, a = 0, d = 0;
        a += 150.0f * cover(r, (float)g->r_plot, 1.0f);
        a += 230.0f * cover(r, (float)g->r_out, 1.3f);
        if (r > g->r_out) { float h = clampf(1.0f - (r - g->r_out) / 5.0f, 0, 1); a += 30.0f * h * h; }
        if (r < g->r_plot && r > g->r_plot - 10) { float h = (r - (g->r_plot - 10)) / 10.0f; a += 22.0f * h * h; }
        for (int k = 0; k < 4; k++) d += 95.0f * cover(r, rssi_frac((float)DBM_RINGS[k]) * g->r_plot, 1.0f);
        g->ring[i] = (uint8_t)clampf(a, 0, 255);
        g->dash[i] = (uint8_t)clampf(d, 0, 255);
    }
    for (int r = 0; r <= g->r_plot + 1; r++) {
        float t = (float)r / (float)g->r_plot;
        g->glass[r] = RADAR_EPAPER ? 255 : (uint8_t)(70.0f + 100.0f * t * t * t);   // paper: flat light grey face, like ViewOwl's dials
    }
}

static void set_geo(rgeo_t *g, int cx, int cy, int r_out, int lbl, int plot)
{
    g->cx = cx; g->cy = cy; g->r_out = r_out; g->r_lbl = lbl; g->r_plot = plot;
}

int radar_init(void)
{
    // full screen radar: as big as the height allows. on 320x240 this is 160,120 r=115
    int h2 = GFX_H / 2, rf = h2 - 5;
    set_geo(&G_FULL, GFX_W / 2, h2, rf, rf - 13, rf - 23);
#if RADAR_ROUND
    // round glass: no room for a side panel, main screen is the full radar. a bit smaller
    // so the outer ring clears the bezel (ViewOwl keeps everything inside r 112 on 240)
    rf = GFX_W / 2 - 12;
    set_geo(&G_FULL, GFX_W / 2, h2, rf, rf - 13, rf - 23);
    set_geo(&G_MAIN, GFX_W / 2, h2, rf, rf - 13, rf - 23);
    UI_PX = GFX_W;
    UI_PW = 0;
#elif RADAR_EPAPER
    // paper dashboard (ui_paper.c): grey header band on top, radar under it on the left,
    // cards from UI_PX on. the 4.2" keeps it smaller so the cards still fit next to it
    int rm = (GFX_H - PAPER_HEAD - 16) / 2;
    if (rm > GFX_W / 5 - 5) rm = GFX_W / 5 - 5;
    set_geo(&G_MAIN, rm + 12, PAPER_HEAD + 8 + rm, rm, rm - 13, rm - 22);
    UI_PX = G_MAIN.cx + rm + 14;
    UI_PW = GFX_W - UI_PX - 6;
#else
    // main screen: radar on the left, panel takes the rest (320x240: 113,120 r=106, panel at 228)
    int rm = h2 - 14;
    set_geo(&G_MAIN, rm + 7, h2, rm, rm - 13, rm - 22);
    UI_PX = 2 * G_MAIN.cx + 2;
    UI_PW = GFX_W - UI_PX - 1;
#endif
    if (s_r8) return 1;
    s_r8 = (uint8_t *)malloc(RQ_MAX * RQ_MAX);
    s_phi8 = (uint8_t *)malloc(RQ_MAX * RQ_MAX);
    s_stat = (uint8_t *)malloc(RQ_MAX * RQ_MAX);
    if (!s_r8 || !s_phi8 || !s_stat) return 0;
    for (int dy = 0; dy < RQ_MAX; dy++)
        for (int dx = 0; dx < RQ_MAX; dx++) {
            float r = sqrtf((float)(dx * dx + dy * dy));
            float a = atan2f((float)dx, (float)dy) / (0.5f * 3.14159265f) * 256.0f;
            s_r8[dy * RQ_MAX + dx] = (uint8_t)(r + 0.5f > 255.0f ? 255.0f : r + 0.5f);
            s_phi8[dy * RQ_MAX + dx] = (uint8_t)(a + 0.5f > 255.0f ? 255.0f : a + 0.5f);
        }
    build_geo(&G_MAIN);
    build_geo(&G_FULL);
    return 1;
}

/* --- layout --- */

typedef struct { int x, y, w, h; } rect_t;

static int overlap(rect_t a, rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void deg_label(const rgeo_t *g, int k, char *buf, int *x, int *y)
{
    int deg = k * 30;
    snprintf(buf, 16, "%d", deg);
    float a = (float)deg * D2R;
    *x = (int)lroundf(g->cx + g->r_lbl * sinf(a)) - micro_width(buf, 1) / 2;
    *y = (int)lroundf(g->cy - g->r_lbl * cosf(a)) - 2;
}

void radar_place(ui_t *u, const rgeo_t *g)
{
    const int full = g == &G_FULL;
    for (int i = 0; i < u->n; i++) {
        blip_t *b = &u->b[i];
        anim_t *an = 0;
        for (int s = 0; s < MAX_CONTACTS; s++) if (u->anim[s].id == u->snap.c[i].id) an = &u->anim[s];
        float t = an ? an->t : rssi_frac(u->snap.c[i].rssi);
        b->r = t * (float)g->r_plot;
        b->x = g->cx + b->r * sinf(b->ang * D2R);
        b->y = g->cy - b->r * cosf(b->ang * D2R);
        b->label = 0;
    }

    rect_t used[MAX_CONTACTS + 24];
    int nu = 0;
    for (int k = 0; k < 12; k++) {
        char buf[16]; int x, y;
        deg_label(g, k, buf, &x, &y);
        used[nu++] = (rect_t){ x - 1, y - 1, micro_width(buf, 1) + 2, 8 };
    }
    for (int k = 0; k < 4; k++) {
        int r = (int)lroundf(rssi_frac((float)DBM_RINGS[k]) * g->r_plot);
        used[nu++] = (rect_t){ g->cx + 2, g->cy - r - 8, 14, 8 };
    }
    if (RADAR_ROUND) {                            // note box under the centre (ui_round.c)
        used[nu++] = (rect_t){ g->cx - 60, g->cy + 48, 120, 28 };
    } else if (full) {                            // corner readouts
        used[nu++] = (rect_t){ 0, 0, 62, 40 };
        used[nu++] = (rect_t){ GFX_W - 62, 0, 62, 40 };
        used[nu++] = (rect_t){ 0, GFX_H - 40, 70, 40 };
        used[nu++] = (rect_t){ GFX_W - 70, GFX_H - 40, 70, 40 };
    }
    for (int i = 0; i < u->n && nu < MAX_CONTACTS + 24; i++)
        used[nu++] = (rect_t){ (int)u->b[i].x - 4, (int)u->b[i].y - 4, 9, 9 };

    int sweep = u->snap.sweep;
    if (sweep == SWEEP_COUNTDOWN) return;         // no labels during countdown
    int only = -1;
    if (sweep == SWEEP_RUN) only = u->target >= 0 ? u->target : (u->n ? u->order[0] : -1);

    int budget = full ? (u->target >= 0 ? 7 : 10) : (u->target >= 0 ? 4 : 6), placed = 0;
    int xmax = full ? GFX_W - 2 : PX - 5;
    static rect_t used2[MAX_CONTACTS + 40];
    memcpy(used2, used, sizeof(rect_t) * (size_t)nu);
    for (int j = -1; j < u->n && placed < budget; j++) {
        int i = j < 0 ? u->target : u->order[j];
        if (i < 0 || (j >= 0 && i == u->target)) continue;
        if (only >= 0 && i != only) continue;
        blip_t *b = &u->b[i];
        int w = font_width(&FONT_SMALL, b->name);
        if (w > LABEL_W) w = LABEL_W;
        int ws = micro_width(b->sub, 1);
        if (ws > w) w = ws;
        w += 5;
        int h = 18;
        // spots to try in order: right, left, then the four corners
        const int cand[6][2] = { { 7, -9 }, { -7 - w, -9 }, { 5, -h - 4 }, { 5, 5 }, { -5 - w, -h - 4 }, { -5 - w, 5 } };
        for (int k = 0; k < 6; k++) {
            rect_t r = { (int)b->x + cand[k][0], (int)b->y + cand[k][1], w, h };
            if (r.x < 3 || r.x + r.w > xmax || r.y < 3 || r.y + r.h > GFX_H - 3) continue;
#if RADAR_ROUND
            // every corner of the label has to be on the glass
            int out = 0;
            for (int q = 0; q < 4; q++) {
                float qx = (float)(q & 1 ? r.x + r.w : r.x) - g->cx, qy = (float)(q & 2 ? r.y + r.h : r.y) - g->cy;
                out |= qx * qx + qy * qy > (float)(g->r_out * g->r_out);
            }
            if (out) continue;
#endif
            int clash = 0;
            for (int q = 0; q < nu && !clash; q++) clash = overlap(r, used2[q]);
            if (clash) continue;
            b->label = 1;
            b->lx = (int16_t)r.x; b->ly = (int16_t)r.y; b->lw = (int16_t)r.w; b->lh = (int16_t)r.h;
            used2[nu++] = r;
            placed++;
            break;
        }
    }
}

int radar_hit(const ui_t *u, const rgeo_t *g, int x, int y)
{
    int best = -1;
    float bd = g == &G_FULL ? 16.0f : 14.0f;   // touch radius, px
    for (int i = 0; i < u->n; i++) {
        float d = hypotf(u->b[i].x - (float)x, u->b[i].y - (float)y);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* --- drawing --- */

// 72 ticks around (every 5 deg), long one every 30, medium every 10
static float tick_alpha(uint16_t ang, unsigned r16, int r_out)
{
    uint32_t t = (uint32_t)ang * 72u + 32768u;
    unsigned k = (t >> 16) % 72u;
    int frac = (int)(t & 0xFFFFu) - 32768;
    float px = (float)frac * (float)r16 * (6.2831853f / 72.0f / 65536.0f / 16.0f);
    float cov = 0.75f - fabsf(px);
    if (cov <= 0) return 0;
    int len = (k % 6 == 0) ? 6 : (k % 2 == 0) ? 4 : 2;
    float a = (k % 6 == 0) ? 240.0f : (k % 2 == 0) ? 150.0f : 90.0f;
    if (r16 < (unsigned)((r_out - len) * 16) || r16 > (unsigned)(r_out * 16 + 4)) return 0;
    return a * clampf(cov, 0, 1);
}

/* everything on the disc that never moves (rings, dashes, ticks, cross) baked into one
 * light value per pixel of one quadrant (s_stat). other 3 quadrants mirror it, 90 deg
 * holds a whole number of every tick and dash. doing it per pixel per frame was 22 ms on the board */

static void build_static(const rgeo_t *g)
{
    const int rq = g->r_out + 5;
    for (int ady = 0; ady < RQ_MAX; ady++)
        for (int adx = 0; adx < RQ_MAX; adx++) {
            float a = 0;
            if (ady < rq && adx < rq) {
                // exact math here, runs once. the lookup tables only have whole pixels
                float rf = sqrtf((float)(adx * adx + ady * ady));
                unsigned r16 = (unsigned)(rf * 16.0f + 0.5f);
                uint16_t phi = (uint16_t)(atan2f((float)adx, (float)ady) / (2.0f * 3.14159265f) * 65536.0f + 0.5f);
                if (r16 < (unsigned)rq * 16) {
                    a = g->ring[r16];
                    if ((phi >> 9) & 1) a += g->dash[r16];
                    a += tick_alpha(phi, r16, g->r_out);
                    if (r16 <= (unsigned)g->r_plot * 16) {
                        if (adx == 0 || ady == 0) a += 34.0f;
                        else if (adx == ady && !(adx & 1)) a += 20.0f;
                    }
                }
            }
            s_stat[ady * RQ_MAX + adx] = (uint8_t)clampf(a, 0, 255);
        }
    s_stat_geo = g;
}

/* glass, rings, cross, ticks and beam in one pass over the disc,
 * using the polar lookup so no trig per pixel */
CORE_HOT static void draw_disc(ui_t *u, canvas_t *cv, const rgeo_t *g)
{
    const int rq = g->r_out + 5;
    int ya = g->cy - rq + 1, yb = g->cy + rq - 1;
    if (ya < cv->y0) ya = cv->y0;
    if (yb > cv->y0 + cv->h - 1) yb = cv->y0 + cv->h - 1;
    if (ya > yb) return;

    const int sweep = u->snap.sweep;
    const int run = sweep == SWEEP_RUN;
    const int beam_on = RADAR_EPAPER ? run : sweep != SWEEP_COUNTDOWN;   // paper: a frozen beam means nothing, only show it while turning
    const uint16_t beam = (uint16_t)(u->beam / 360.0f * 65536.0f);
    const uint16_t trail = run ? 7282 : 16384;          /* 40 or 90 degrees */
    const float trail_a = run ? 90.0f : 72.0f;
    const unsigned RP = (unsigned)g->r_plot, RMAX = (unsigned)(rq - 1);
    const float arc_k = 6.2831853f / 65536.0f;
    const uint32_t swept = gfx_scale(C_PRIM, 16);
    if (s_stat_geo != g) build_static(g);
    const uint32_t glass = C_GLASS, prim = C_PRIM;
    // trail light by distance behind the beam, 256 steps so theres no divide per pixel
    uint8_t trail_lut[257];
    for (int i = 0; i <= 256; i++) {
        float f = 1.0f - (float)i / 256.0f;
        trail_lut[i] = (uint8_t)(trail_a * f * f);
    }
    const uint32_t trail_k = (256u << 16) / trail;

    for (int y = ya; y <= yb; y++) {
        int dy = y - g->cy, ady = dy < 0 ? -dy : dy;
        if (ady >= rq) continue;
        uint32_t *row = gfx_row(cv, y);
        const uint8_t *lr = s_r8 + ady * RQ_MAX, *lp = s_phi8 + ady * RQ_MAX;
        const uint8_t *ls = s_stat + ady * RQ_MAX;
        int span = (int)sqrtf((float)((rq - 1) * (rq - 1) - ady * ady));
        int xa = g->cx - span < 0 ? 0 : g->cx - span;
        int xb = g->cx + span > GFX_W - 1 ? GFX_W - 1 : g->cx + span;
        for (int x = xa; x <= xb; x++) {
            int dx = x - g->cx, adx = dx < 0 ? -dx : dx;
            unsigned r = lr[adx];
            if (r > RMAX) continue;
            unsigned q = (unsigned)lp[adx] << 6;           /* quarter turn = 16384 */
            uint16_t ang;
            if (dx >= 0) ang = dy < 0 ? (uint16_t)q : (uint16_t)(32768u - q);
            else ang = dy >= 0 ? (uint16_t)(32768u + q) : (uint16_t)(0u - q);

            uint32_t p = row[x];
            if (r <= RP) p = gfx_mix(p, glass, g->glass[r]);
            unsigned st = ls[adx];
            if (st) p = gfx_add(p, gfx_scale(prim, st));

            if (beam_on && r <= RP) {
                uint16_t d = (uint16_t)(beam - ang);       /* how far behind the beam */
                if (d < trail) {
                    p = gfx_add(p, gfx_scale(prim, trail_lut[((uint32_t)d * trail_k) >> 16]));
                } else if (run && d <= beam) {
                    p = gfx_add(p, swept);
                }
                int16_t e = (int16_t)(uint16_t)(ang - beam);   // signed angle to the beam edge
                if (e > -1200 && e < 1200 && r > 2) {
                    float px = fabsf((float)e * (float)r * arc_k);
                    float w = run ? 1.6f : 1.1f;
                    if (px < w) p = gfx_add(p, gfx_scale(C_FOCUS, (uint32_t)((run ? 230 : 150) * clampf(w - px, 0, 1))));
                }
            }
            row[x] = p;
        }
    }
}

static void draw_scale_text(canvas_t *cv, const rgeo_t *g)
{
    for (int k = 0; k < 12; k++) {
        char buf[16]; int x, y;
        deg_label(g, k, buf, &x, &y);
        micro_text(cv, x, y, buf, k == 0 ? C_FOCUS : C_PRIM, 220, 1);
    }
    for (int k = 0; k < 4; k++) {
        char buf[16];
        snprintf(buf, sizeof buf, "%d", DBM_RINGS[k]);
        int r = (int)lroundf(rssi_frac((float)DBM_RINGS[k]) * g->r_plot);
        micro_text(cv, g->cx + 3, g->cy - r - 7, buf, C_QUIET, 230, 1);
    }
    float ty = (float)(g->cy - g->r_out - 8);
    if (ty >= 0)    // forward mark, up = ahead of you
        gfx_tri(cv, g->cx - 4.0f, ty, g->cx + 4.0f, ty, (float)g->cx, ty + 5.0f, C_FOCUS, 220);
}

static void draw_blips(ui_t *u, canvas_t *cv, const rgeo_t *g, int full)
{
    if (!gfx_touches(cv, g->cy - g->r_plot - 30, g->cy + g->r_plot + 30)) return;
    const float sz = full ? 1.2f : 1.0f;

    if (u->target >= 0 && u->b[u->target].ghost)       // ghost target: "somewhere on this ring"
        gfx_ring(cv, (float)g->cx, (float)g->cy, u->b[u->target].r, 1.0f, C_FOCUS, 70);

    for (int j = u->n - 1; j >= 0; j--) {               // quietest first so loud ones end up on top
        int i = u->order[j];
        const blip_t *b = &u->b[i];
        float k = b->bright * (b->dim ? 0.4f : 1.0f);
        if (RADAR_EPAPER) {                             // paper: no glow, it only prints as grey fuzz. solid ink
            if (!b->ghost && (b->label || b->sel))
                gfx_arc(cv, (float)g->cx, (float)g->cy, b->r, b->ang - b->sd, b->ang + b->sd, 1.3f, b->col, 255);
            if (b->ghost) gfx_ring(cv, b->x, b->y, 3.4f * sz, 1.2f, b->col, 255);
            else gfx_disc(cv, b->x, b->y, 3.2f * sz, b->col, 255);
            continue;
        }
        if (!b->ghost) {
            if (b->label || b->sel)                     // angle error, as the arc where it could be
                gfx_arc(cv, (float)g->cx, (float)g->cy, b->r, b->ang - b->sd, b->ang + b->sd, 1.3f,
                        b->col, (int)(150 * (b->dim ? 0.4f : 1.0f)));
            gfx_glow(cv, b->x, b->y, 10.0f * sz, b->col, (int)(170 * k));
            gfx_disc(cv, b->x, b->y, 2.1f * sz, gfx_mix(b->col, C_FOCUS, 140), (int)(256 * clampf(k + 0.25f, 0, 1)));
            if (b->src >= 2)                            // placed by the walk map: extra ring
                gfx_ring(cv, b->x, b->y, 4.6f * sz, 0.8f, b->col, (int)(150 * k));
        } else {
            gfx_glow(cv, b->x, b->y, 7.0f * sz, b->col, (int)(70 * k));
            gfx_ring(cv, b->x, b->y, 3.2f * sz, 1.0f, b->col, (int)(230 * k));
        }
        if (!RADAR_EPAPER && b->age_ms < 1600) {        // expanding ring for the first 1.6 s
            float f = (float)b->age_ms / 1600.0f;
            gfx_ring(cv, b->x, b->y, 4.0f + 16.0f * f, 1.0f, b->col, (int)(200 * (1.0f - f)));
        }
    }

    if (u->target >= 0) {
        const blip_t *b = &u->b[u->target];
        float rot = (float)(u->now % 3000u) * (360.0f / 3000.0f);   // one turn per 3 s
        if (!b->ghost) gfx_line(cv, (float)g->cx, (float)g->cy, b->x, b->y, 1.0f, C_FOCUS, 60);
        for (int q = 0; q < 4; q++)
            gfx_arc(cv, b->x, b->y, 10.0f * sz, rot + q * 90.0f - 20.0f, rot + q * 90.0f + 20.0f, 1.4f, C_FOCUS, 230);
    }

    for (int i = 0; i < u->n; i++) {
        const blip_t *b = &u->b[i];
        if (!b->label) continue;
        float k = b->dim && !RADAR_EPAPER ? 0.55f : 1.0f;   // half ink on paper is dots, unreadable
        gfx_fill(cv, b->lx, b->ly, b->lw, b->lh, C_BG0, RADAR_EPAPER ? 255 : 150);   // paper: clean white under the words
        gfx_vline(cv, b->lx, b->ly, b->ly + b->lh - 1, b->col, 230);
        font_text_clip(cv, &FONT_SMALL, b->lx + 3, b->ly, b->name, b->sel ? C_FOCUS : b->col, (int)(255 * k), LABEL_W);
        micro_text(cv, b->lx + 3, b->ly + 11, b->sub, RADAR_EPAPER ? C_FOCUS : C_DIM, (int)(230 * k), 1);
        float nx = clampf(b->x, (float)b->lx, (float)(b->lx + b->lw - 1));
        float ny = clampf(b->y, (float)b->ly, (float)(b->ly + b->lh - 1));
        float dx = nx - b->x, dy = ny - b->y, l = sqrtf(dx * dx + dy * dy);
        if (l > 6.0f)
            gfx_line(cv, b->x + dx / l * 4.5f, b->y + dy / l * 4.5f, nx, ny, 1.0f, b->col, (int)(120 * k));
    }
}

/* during the 360 turn: signal of the target (or the loudest one) around the circle,
 * drawn while it is measured. the lobe points to the network */
static void draw_rose(ui_t *u, canvas_t *cv, const rgeo_t *g)
{
    const app_t *a = &u->snap;
    int i = u->target >= 0 ? u->target : (u->n ? u->order[0] : -1);
    if (i < 0) return;
    const contact_t *c = &a->c[i];
    float raw[BEAR_BINS], m[BEAR_BINS], lo = 1e9f, hi = -1e9f;
    for (int k = 0; k < BEAR_BINS; k++)
        raw[k] = c->bin_n[k] ? c->bin_sum[k] / c->bin_n[k] : 0.0f;
    for (int k = 0; k < BEAR_BINS; k++) {         // 1-2-1 average over 3 bins, so the lobe shows
        if (!c->bin_n[k]) continue;
        float s = 0, w = 0;
        for (int d = -1; d <= 1; d++) {
            int q = (k + d + BEAR_BINS) % BEAR_BINS;
            if (!c->bin_n[q]) continue;
            float wt = d ? 1.0f : 2.0f;
            s += raw[q] * wt;
            w += wt;
        }
        m[k] = s / w;
        if (m[k] < lo) lo = m[k];
        if (m[k] > hi) hi = m[k];
    }
    if (hi < lo) return;
    const float r0 = g->r_plot * 0.19f, rs = g->r_plot * 0.71f;
    float px = 0, py = 0, fx = 0, fy = 0;
    int prev = 0, first = 1;
    for (int k = 0; k < BEAR_BINS; k++) {
        if (!c->bin_n[k]) { prev = 0; continue; }
        float r = r0 + rs * (m[k] - lo) / (hi - lo + 2.0f);   // +2 so flat data doesnt blow up to full size
        float th = ((float)k + 0.5f) * (360.0f / BEAR_BINS) * D2R;
        float x = g->cx + r * sinf(th), y = g->cy - r * cosf(th);
        if (prev) gfx_line(cv, px, py, x, y, 1.4f, C_FOCUS, 190);
        gfx_disc(cv, x, y, 1.4f, C_FOCUS, 230);
        if (first && k == 0) { fx = x; fy = y; first = 0; }
        px = x; py = y; prev = 1;
    }
    if (c->bin_n[0] && c->bin_n[BEAR_BINS - 1] && !first)
        gfx_line(cv, px, py, fx, fy, 1.4f, C_FOCUS, 190);
}

static void draw_sweep_overlay(ui_t *u, canvas_t *cv, const rgeo_t *g)
{
    const app_t *a = &u->snap;
    char buf[16];
    if (a->sweep == SWEEP_COUNTDOWN) {
        uint32_t el = u->now - a->sweep_t0;
        int n = 3 - (int)(el / 1000u);
        float f = (float)(el % 1000u) / 1000.0f;
        snprintf(buf, sizeof buf, "%d", n < 1 ? 1 : n);
        int w = font_width(&FONT_LARGE, buf);
        gfx_disc(cv, (float)g->cx, (float)g->cy, 19.0f, C_BG0, 200);
        gfx_ring(cv, (float)g->cx, (float)g->cy, 19.0f, 1.2f, C_PRIM, 200);
        gfx_ring(cv, (float)g->cx, (float)g->cy, 20.0f + 50.0f * f, 1.5f, C_FOCUS, (int)(200 * (1.0f - f)));
        font_text(cv, &FONT_LARGE, g->cx - w / 2, g->cy - 14, buf, C_FOCUS, 255);
    } else if (a->sweep == SWEEP_RUN) {
        draw_rose(u, cv, g);
        gfx_arc(cv, (float)g->cx, (float)g->cy, g->r_out + 3.0f, 0.0f, u->beam, 2.0f, C_FOCUS, 200);
        float th = u->beam * D2R, s = sinf(th), c = cosf(th);
        float tx = g->cx + (g->r_out - 1) * s, ty = g->cy - (g->r_out - 1) * c;
        float bx = g->cx + (g->r_out - 11) * s, by = g->cy - (g->r_out - 11) * c;
        gfx_tri(cv, tx - 5 * c, ty - 5 * s, tx + 5 * c, ty + 5 * s, bx, by, C_FOCUS, 240);
    } else if (a->sweep == SWEEP_RESULT) {
        float f = clampf((float)(u->now - a->sweep_t0) / 900.0f, 0, 1);
        gfx_ring(cv, (float)g->cx, (float)g->cy, g->r_plot * f, 2.0f, C_FOCUS, (int)(220 * (1.0f - f)));
    }
}

static void draw_boot(ui_t *u, canvas_t *cv, const rgeo_t *g)
{
    uint32_t t = u->now - u->boot;
    if (t < 1800u) {   // boot ring and logo, 1.8 s
        float f = (float)t / 1800.0f;
        gfx_ring(cv, (float)g->cx, (float)g->cy, 8.0f + g->r_out * f, 2.0f, C_FOCUS, (int)(230 * (1.0f - f)));
        int w = micro_width(RADAR_NAME, 2);
        gfx_fill(cv, g->cx - w / 2 - 4, g->cy + 14, w + 8, 18, C_BG0, (int)(200 * (1.0f - f * f)));
        micro_text(cv, g->cx - w / 2, g->cy + 17, RADAR_NAME, C_FOCUS, (int)(255 * (1.0f - f * f)), 2);
    }
    if (u->snap.n == 0 && u->snap.sweep == SWEEP_IDLE && t >= 1800u && (RADAR_EPAPER || ((u->now / 700u) & 1))) {
        int w = micro_width("LISTENING", 1);
        micro_text(cv, g->cx - w / 2, g->cy + 10, "LISTENING", C_FOCUS, 230, 1);
    }
}

void radar_draw(ui_t *u, canvas_t *cv, const rgeo_t *g, int full)
{
    draw_disc(u, cv, g);
    ui_lap(u, 1);
    draw_scale_text(cv, g);
    draw_blips(u, cv, g, full);
    draw_sweep_overlay(u, cv, g);
    draw_boot(u, cv, g);
    ui_lap(u, 5);
}
