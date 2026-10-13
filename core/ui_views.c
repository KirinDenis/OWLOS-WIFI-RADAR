/* full screen views: network list, spectrum, target, walk map, touch calibration.
 * touch handling for them is in ui.c */
#include "ui_int.h"
#include <stdlib.h>

/* --- network list, one per row --- */

int list_row_at(const ui_t *u, int y)
{
    if (y < LIST_Y0 || y >= LIST_Y1) return -1;
    int k = (y - LIST_Y0 + u->scroll) / ROW_H;
    return k < u->snap.n ? u->list[k] : -1;
}

void view_list(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char buf[64];
    for (int k = 0; k < a->n; k++) {
        int ry = LIST_Y0 + k * ROW_H - u->scroll;
        if (ry > GFX_H) break;
        if (ry + ROW_H < 18 || !gfx_touches(cv, ry, ry + ROW_H)) continue;
        int i = u->list[k];
        const contact_t *c = &a->c[i];
        const blip_t *b = &u->b[i];
        if (i == u->target) gfx_fill(cv, 0, ry, GFX_W - 4, ROW_H - 1, C_FOCUS, 26);
        gfx_fill(cv, 3, ry + 2, 2, ROW_H - 5, b->col, 230);
        font_text_clip(cv, &FONT_MEDIUM, 10, ry + 1, net_name(c), gfx_mix(b->col, C_FOCUS, 120), 255, GFX_W - 148);
        float d = app_distance_m(c->rssi);
        int n = snprintf(buf, sizeof buf, "CH %d  %s  ~%d M", c->channel, sec_name(c->sec), (int)lroundf(d));
        if (b->src) snprintf(buf + n, sizeof buf - (size_t)n, "  BRG %03d", (int)lroundf(b->ang) % 360);
        micro_text(cv, 10, ry + 15, buf, C_DIM, 225, 1);
        float f = clampf((c->rssi + 95.0f) / 65.0f, 0, 1);
        gfx_fill(cv, GFX_W - 130, ry + 8, 64, 4, C_QUIET, 50);
        gfx_fill(cv, GFX_W - 130, ry + 8, (int)(64 * f), 4, b->col, 220);
        float tr = app_trend(c);
        if (tr > TREND_DB) arrow(cv, GFX_W - 56.0f, ry + 10.0f, 1, C_FOCUS);
        else if (tr < -TREND_DB) arrow(cv, GFX_W - 56.0f, ry + 10.0f, 0, C_DIM);
        snprintf(buf, sizeof buf, "%d", (int)lroundf(c->rssi));
        font_text(cv, &FONT_MEDIUM, GFX_W - 8 - font_width(&FONT_MEDIUM, buf), ry + 3, buf, C_FOCUS, 255);
        gfx_hline(cv, 8, GFX_W - 8, ry + ROW_H - 1, C_QUIET, 45);
    }
    int total = a->n * ROW_H, vis = GFX_H - LIST_Y0;
    if (total > vis) {                                  // scrollbar
        int th = vis * vis / total, ty = LIST_Y0 + (vis - th) * u->scroll / (total - vis);
        gfx_fill(cv, GFX_W - 3, LIST_Y0, 2, vis, C_QUIET, 60);
        gfx_fill(cv, GFX_W - 3, ty, 2, th, C_FOCUS, 200);
    }
    snprintf(buf, sizeof buf, "NETWORKS %d", a->n);
    view_header(cv, buf, 0);
    static const char *keys[3] = { "SIGNAL", "NAME", "CH" };    // tap() in ui.c expects these at x >= GFX_W - 120, 40 px each
    for (int s = 0; s < 3; s++) {
        int x = GFX_W - 120 + s * 40, w = micro_width(keys[s], 1);
        gfx_fill(cv, x, 2, 38, 13, C_PANEL, 255);
        micro_text(cv, x + (38 - w) / 2, 6, keys[s], s == u->sort ? C_FOCUS : C_QUIET, 240, 1);
        if (s == u->sort) gfx_hline(cv, x + 2, x + 35, 14, C_FOCUS, 230);
    }
}

/* --- spectrum, drawn like a wifi analyser app --- */

// plot box: left, right, top, height. round glass pulls it in from the edges
#if RADAR_ROUND
#define SP_L 30
#define SP_R (GFX_W - 30)
#define SP_T 46
#define SP_H 120
#else
#define SP_L 22
#define SP_R (GFX_W - 6)
#define SP_T 24
#define SP_H (GFX_H - 68)
#endif
#define SP_PITCH ((SP_R - SP_L - 28) / 12.0f)                                         // 22 px a channel on 320 wide
static float spx(float ch) { return SP_L + 8.0f + (ch - 1.0f) * SP_PITCH; }            // channel -> x
static float spy(float dbm) { return SP_T + (-30.0f - dbm) / 65.0f * SP_H; }           // dBm -> y, -30 at the top

int spectrum_channel_at(int x)
{
    int c = (int)lroundf(((float)x - SP_L - 8.0f) / SP_PITCH) + 1;
    return c < 1 ? 1 : c > 13 ? 13 : c;
}

int spectrum_channel_x(int ch)
{
    return (int)lroundf(spx((float)ch));
}

// rough shape of a 20 MHz channel: flat top, falls off past ~1.4 channels each side
static float hump(float rssi, float d)
{
    float e = fabsf(d) - 1.4f;
    return e > 0 ? rssi - 40.0f * e * e : rssi;
}

void view_spectrum(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char buf[64];
    const float base = spy(-95.0f);
    for (int db = -40; db >= -90; db -= 10) {
        int y = (int)lroundf(spy((float)db));
        gfx_hline(cv, SP_L, SP_R, y, C_QUIET, db % 20 == 0 ? 70 : 35);
        snprintf(buf, sizeof buf, "%d", db);
        if (RADAR_ROUND) micro_text(cv, SP_L + 2, y - 7, buf, C_QUIET, 220, 1);   // no room left of the plot
        else micro_text(cv, SP_L - 20, y - 2, buf, C_QUIET, 220, 1);
    }
    for (int ch = 1; ch <= 13; ch++) {
        int x = (int)lroundf(spx((float)ch));
        if (ch == u->chan_sel) gfx_fill(cv, x - (int)(SP_PITCH / 2) + 1, SP_T - 2, (int)SP_PITCH - 1, (int)base - SP_T + 2, C_FOCUS, 18);
        gfx_vline(cv, x, (int)base, (int)base + 3, C_QUIET, 200);
        snprintf(buf, sizeof buf, "%d", ch);
        micro_text(cv, x - micro_width(buf, 1) / 2, (int)base + 5, buf, ch == a->channel ? C_FOCUS : C_DIM, 230, 1);
    }

    int count = u->n < 16 ? u->n : 16;
    for (int j = count - 1; j >= 0; j--) {                // weakest first, so loud ones end up on top
        int i = u->order[j];
        const contact_t *c = &a->c[i];
        uint32_t col = i == u->target ? C_FOCUS : u->b[i].col;
        int hot = u->chan_sel < 0 || c->channel == u->chan_sel || i == u->target;
        int x0 = (int)spx((float)c->channel - 2.3f), x1 = (int)spx((float)c->channel + 2.3f);
        float px = 0, py = 0;
        for (int x = x0; x <= x1; x += 2) {
            float lvl = hump(c->rssi, ((float)x - spx((float)c->channel)) / SP_PITCH);
            if (lvl < -95.0f) lvl = -95.0f;
            float y = spy(lvl);
            if (x >= SP_L && x < SP_R) {
                gfx_vline(cv, x, (int)y, (int)base, col, hot ? 26 : 10);
                gfx_vline(cv, x + 1, (int)y, (int)base, col, hot ? 26 : 10);
            }
            if (x > x0 && px >= SP_L && x < SP_R) gfx_line(cv, px, py, (float)x, y, 1.2f, col, hot ? 220 : 70);
            px = (float)x; py = y;
        }
    }
    // names on the peaks, moved up if they overlap, give up after a few tries
    int lx[10], ly[10], lw[10], nl = 0;
    for (int j = 0; j < count && nl < 10; j++) {
        int i = u->order[j];
        const contact_t *c = &a->c[i];
        if (u->chan_sel >= 0 && c->channel != u->chan_sel && i != u->target) continue;
        int w = font_width(&FONT_SMALL, net_name(c));
        if (w > 64) w = 64;
        int x = (int)spx((float)c->channel) - w / 2, y = (int)spy(c->rssi) - 12;
        if (x < SP_L) x = SP_L;
        if (x + w > SP_R) x = SP_R - w;
        int ok = 0;
        for (int tries = 0; tries < 4 && !ok; tries++) {
            ok = y >= SP_T - 4;
            for (int q = 0; q < nl && ok; q++)
                if (x < lx[q] + lw[q] && lx[q] < x + w && y < ly[q] + 10 && ly[q] < y + 10) ok = 0;
            if (!ok) y -= 11;
        }
        if (!ok) continue;
        lx[nl] = x; ly[nl] = y; lw[nl] = w; nl++;
        font_text_clip(cv, &FONT_SMALL, x, y, net_name(c), i == u->target ? C_FOCUS : u->b[i].col, 255, 64);
    }

#if RADAR_ROUND
    if (u->chan_sel > 0) {                              // round: just the count, centred
        int n = 0, len = snprintf(buf, sizeof buf, "CH %d: ", u->chan_sel);
        for (int j = 0; j < u->n; j++) if (a->c[u->order[j]].channel == u->chan_sel) n++;
        snprintf(buf + len, sizeof buf - (size_t)len, "%d NET%s", n, n == 1 ? "" : "S");
    } else {
        snprintf(buf, sizeof buf, "TAP A CHANNEL");
    }
    micro_text(cv, GFX_W / 2 - micro_width(buf, 1) / 2, SP_T + SP_H + 22, buf, u->chan_sel > 0 ? C_FOCUS : C_DIM, 235, 1);
#else
    if (u->chan_sel > 0) {
        int n = 0, len = 0;
        len += snprintf(buf, sizeof buf, "CH %d:", u->chan_sel);
        for (int j = 0; j < u->n; j++) if (a->c[u->order[j]].channel == u->chan_sel) n++;
        snprintf(buf + len, sizeof buf - (size_t)len, " %d NETWORK%s", n, n == 1 ? "" : "S");
        micro_text(cv, 8, GFX_H - 26, buf, C_FOCUS, 240, 1);
        int x = 8;
        for (int j = 0; j < u->n && x < GFX_W - 40; j++) {
            const contact_t *c = &a->c[u->order[j]];
            if (c->channel != u->chan_sel) continue;
            x += font_text_clip(cv, &FONT_SMALL, x, GFX_H - 17, net_name(c), u->b[u->order[j]].col, 255, 70) + 10;
        }
    } else {
        micro_text(cv, 8, GFX_H - 22, RADAR_EPAPER ? "DIAL: PICK A CHANNEL TO SEE WHO SHARES IT" : "TAP A CHANNEL TO SEE WHO SHARES IT", C_DIM, 230, 1);
    }
#endif
    snprintf(buf, sizeof buf, "TUNED CH %02d", a->channel);
    view_header(cv, "SPECTRUM 2.4 GHZ", buf);
}

/* --- target: one network, everything big --- */

void view_target(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    const contact_t *c = &a->c[u->target];
    const blip_t *b = &u->b[u->target];
    char buf[64];

    const int mid = GFX_W / 2;                         // right half starts here (160 on 320 wide)
    font_text_clip(cv, &FONT_MEDIUM, 8, 21, net_name(c), C_FOCUS, 255, GFX_W - 20);
    snprintf(buf, sizeof buf, "%02X:%02X:%02X:%02X:%02X:%02X   CH %d   %s", c->bssid[0], c->bssid[1], c->bssid[2],
             c->bssid[3], c->bssid[4], c->bssid[5], c->channel, sec_name(c->sec));
    micro_text(cv, 8, 37, buf, sec_color(c->sec), 230, 1);

    snprintf(buf, sizeof buf, "%d", (int)lroundf(c->rssi));
    int w = font_text(cv, &FONT_HUGE, 4, 48, buf, C_FOCUS, 255);
    micro_text(cv, 6 + w, 78, "DBM", C_DIM, 230, 1);

    float tr = app_trend(c);
    const char *word = tr > TREND_DB ? "WARMER" : tr < -TREND_DB ? "COLDER" : "STEADY";
    uint32_t wc = tr > TREND_DB ? C_FOCUS : C_DIM;
    if (tr > TREND_DB) gfx_tri(cv, (float)mid, 64, mid + 16.0f, 64, mid + 8.0f, 50, wc, 240);
    else if (tr < -TREND_DB) gfx_tri(cv, (float)mid, 52, mid + 16.0f, 52, mid + 8.0f, 66, wc, 240);
    else gfx_fill(cv, mid, 57, 16, 3, wc, 230);
    font_text(cv, &FONT_MEDIUM, mid + 22, 50, word, wc, 255);
    snprintf(buf, sizeof buf, "%+.1f DB / 10 S", (double)tr);
    micro_text(cv, mid + 22, 66, buf, C_DIM, 230, 1);
    float d = app_distance_m(c->rssi);
    if (d < 10.0f) snprintf(buf, sizeof buf, "~%.1f M", (double)d);
    else snprintf(buf, sizeof buf, "~%d M", (int)lroundf(d));
    font_text(cv, &FONT_MEDIUM, mid, 76, buf, C_FOCUS, 255);

    // direction dial, relative to where you face. "?" if we have no bearing
    const float cx = GFX_W - 28.0f, cy = 66, r = 18;
    gfx_ring(cv, cx, cy, r, 1.2f, C_QUIET, 220);
    gfx_tri(cv, cx - 3, cy - r - 5, cx + 3, cy - r - 5, cx, cy - r - 1, C_FOCUS, 230);
    if (b->src) {
        float th = b->ang * D2R;
        gfx_arc(cv, cx, cy, r - 3, b->ang - b->sd, b->ang + b->sd, 2.0f, C_PRIM, 140);
        gfx_line(cv, cx, cy, cx + (r - 2) * sinf(th), cy - (r - 2) * cosf(th), 2.0f, C_FOCUS, 240);
        snprintf(buf, sizeof buf, "%03d", (int)lroundf(b->ang) % 360);
    } else {
        snprintf(buf, sizeof buf, "?");
    }
    micro_text(cv, (int)cx - micro_width(buf, 1) / 2, (int)(cy + r + 4), buf, C_DIM, 230, 1);

    float f = clampf((c->rssi + 92.0f) / 60.0f, 0, 1);
    int segs = (GFX_W - 8) / 12;                       // 26 on 320 wide
    for (int s = 0; s < segs; s++) {
        int lit = (float)s < f * (float)segs;
        gfx_fill(cv, 8 + s * 12, 100, 10, 16, lit ? (s > segs * 3 / 4 ? C_FOCUS : C_PRIM) : C_QUIET, lit ? 220 : 45);
    }
    micro_text(cv, 8, 119, "FAR", C_DIM, 220, 1);
    micro_text(cv, GFX_W - 8 - micro_width("NEAR", 1), 119, "NEAR", C_DIM, 220, 1);
    draw_sparkline(cv, c, 8, 128, GFX_W - 16, GFX_H - 168, 1);

    // 4 buttons along the bottom, a bit more gap in the middle (tap() splits by GFX_W / 4)
    const int by = GFX_H - 34;
#if RADAR_EPAPER
    key_hints(cv, 4, by, GFX_W - 8, 30, "BACK = RADAR    MENU = NEXT SCREEN    DIAL = NEXT NETWORK", "HOLD MENU = 360 TURN");
#else
    const int bw = (GFX_W - 24) / 4;
    button(cv, 4, by, bw, 30, "360 TURN", 0);
    button(cv, 4 + (bw + 4), by, bw, 30, "WALK MAP", 0);
    button(cv, 4 + 2 * (bw + 4) + 4, by, bw, 30, "NEXT", 0);
    button(cv, 4 + 3 * (bw + 4) + 4, by, bw, 30, "CLEAR", 0);
#endif
    int rank = 0;
    for (int k = 0; k < u->n; k++) if (u->order[k] == u->target) rank = k + 1;
    snprintf(buf, sizeof buf, "%02d/%02d", rank, u->n);
    view_header(cv, "TARGET", buf);
}

/* --- walk map, world units are metres --- */

typedef struct { float mx, my, s; } mapfit_t;
#define MAP_CX (GFX_W / 2.0f)
#if RADAR_ROUND
#define MAP_CY (GFX_H / 2.0f)                // round: centre of the glass, no buttons
#define MAP_FIT 150.0f                       // the walk is fitted in a square this big
#else
#define MAP_CY ((GFX_H - 18) / 2.0f)        // middle of the band between header and buttons
#endif
#define MAP_BOT (GFX_H - 38)                 // grid stops here, buttons below

static void to_screen(const mapfit_t *f, float x, float y, float *sx, float *sy)
{
    *sx = MAP_CX + (x - f->mx) * f->s;
    *sy = MAP_CY - (y - f->my) * f->s;
}

/* fit the walk and the networks on screen. unsolved networks and ones further then
 * 14 m from the walk centre are left out, so one far AP won't shrink the whole map.
 * scale is clamped to 4..60 px per m */
static mapfit_t map_fit(const mapview_t *mv)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, cx = 0, cy = 0;
    for (int t = 0; t < mv->T; t++) { cx += mv->px[t]; cy += mv->py[t]; }
    if (mv->T) { cx /= mv->T; cy /= mv->T; }
    for (int i = 0; i < mv->T + mv->n; i++) {
        float x = i < mv->T ? mv->px[i] : mv->ax[i - mv->T];
        float y = i < mv->T ? mv->py[i] : mv->ay[i - mv->T];
        if (i >= mv->T && (!mv->ok[i - mv->T] || hypotf(x - cx, y - cy) > 14.0f)) continue;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
    }
    mapfit_t f = { 0, 0, 20 };
    if (x1 < x0) return f;
    f.mx = 0.5f * (x0 + x1);
    f.my = 0.5f * (y0 + y1);
#if RADAR_ROUND
    float sx = MAP_FIT / fmaxf(x1 - x0, 2.0f), sy = MAP_FIT / fmaxf(y1 - y0, 2.0f);
#else
    float sx = (GFX_W - 44.0f) / fmaxf(x1 - x0, 2.0f), sy = (GFX_H - 90.0f) / fmaxf(y1 - y0, 2.0f);
#endif
    f.s = clampf(fminf(sx, sy), 4.0f, 60.0f);
    return f;
}

void view_map(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    const mapview_t *mv = &a->map;
    char buf[48];
    mapfit_t f = map_fit(mv);

    // metre grid, step depends on zoom
    float step = f.s >= 14.0f ? 1.0f : f.s >= 7.0f ? 2.0f : 5.0f;
    float wx0 = f.mx - (MAP_CX - 4) / f.s, wx1 = f.mx + (GFX_W - MAP_CX) / f.s;
    float wy0 = f.my - (MAP_BOT - MAP_CY) / f.s, wy1 = f.my + (MAP_CY - 18) / f.s;
    for (float gx = floorf(wx0 / step) * step; gx <= wx1; gx += step) {
        float sx, sy;
        to_screen(&f, gx, 0, &sx, &sy);
        int major = fabsf(remainderf(gx, 5.0f)) < 0.01f;
        gfx_vline(cv, (int)lroundf(sx), 18, MAP_BOT, C_QUIET, major ? 70 : 35);
    }
    for (float gy = floorf(wy0 / step) * step; gy <= wy1; gy += step) {
        float sx, sy;
        to_screen(&f, 0, gy, &sx, &sy);
        int major = fabsf(remainderf(gy, 5.0f)) < 0.01f;
        gfx_hline(cv, 0, GFX_W - 1, (int)lroundf(sy), C_QUIET, major ? 70 : 35);
    }

    /* walk path, older part fainter. averaged over 5 s (t-2..t+2) because the
     * solved path jitters about a metre, the person doesnt */
    float px = 0, py = 0;
    for (int t = 0; t < mv->T; t++) {
        float ax = 0, ay = 0;
        int n = 0;
        for (int q = t - 2; q <= t + 2; q++) {
            if (q < 0 || q >= mv->T) continue;
            ax += mv->px[q]; ay += mv->py[q]; n++;
        }
        if (t == mv->T - 1) { ax = mv->px[t] * n; ay = mv->py[t] * n; }   // last point not averaged, it's where you are now
        float sx, sy;
        to_screen(&f, ax / n, ay / n, &sx, &sy);
        if (t) gfx_line(cv, px, py, sx, sy, 1.4f, C_PRIM, 60 + 170 * t / (mv->T > 1 ? mv->T - 1 : 1));
        px = sx; py = sy;
    }

    // networks
    int lx[MAP_N], ly[MAP_N], nl = 0;
    for (int k = 0; k < mv->n; k++) {
        const contact_t *c = app_find(a, mv->id[k]);
        uint32_t col = c ? sec_color(c->sec) : C_DIM;
        if (c && c->id == a->target_id) col = C_FOCUS;
        float sx, sy;
        to_screen(&f, mv->ax[k], mv->ay[k], &sx, &sy);
#if RADAR_ROUND
        int off = hypotf(sx - MAP_CX, sy - MAP_CY) > 96.0f;
#else
        int off = sx < 6 || sx > GFX_W - 6 || sy < 22 || sy > MAP_BOT - 4;
#endif
        if (off) {                              // off screen: arrow on the edge pointing to it
            float dx = sx - MAP_CX, dy = sy - MAP_CY, l = sqrtf(dx * dx + dy * dy);
#if RADAR_ROUND
            float k1 = 96.0f / l;
#else
            float k1 = fminf((MAP_CX - 10) / fmaxf(fabsf(dx), 1e-3f), (MAP_CY - 26) / fmaxf(fabsf(dy), 1e-3f));
#endif
            sx = MAP_CX + dx * k1; sy = MAP_CY + dy * k1;
            float ux = dx / l, uy = dy / l;
            gfx_tri(cv, sx + ux * 6, sy + uy * 6, sx - uy * 4, sy + ux * 4, sx + uy * 4, sy - ux * 4, col, 200);
        } else if (mv->ok[k]) {
            float rr = mv->sd[k] * f.s;
            if (rr < 70.0f) gfx_ring(cv, sx, sy, rr, 1.0f, col, 60);
            gfx_glow(cv, sx, sy, 9.0f, col, 160);
            gfx_disc(cv, sx, sy, 2.4f, gfx_mix(col, C_FOCUS, 140), 255);
        } else {
            gfx_ring(cv, sx, sy, 3.4f, 1.0f, col, 200);
        }
        if (!c) continue;
        int x = (int)sx + 7, y = (int)sy - 9, w = 64;
        if (x + w > GFX_W - 4) x = (int)sx - 7 - w;
        int clash = 0;
        for (int q = 0; q < nl; q++) if (x < lx[q] + w && lx[q] < x + w && y < ly[q] + 17 && ly[q] < y + 17) clash = 1;
        if (clash || y < 20 || y > MAP_BOT - 16) continue;
        if (RADAR_ROUND && (hypotf(x - MAP_CX, y - MAP_CY) > 100.0f || hypotf(x + w - MAP_CX, y + 17 - MAP_CY) > 100.0f)) continue;
        lx[nl] = x; ly[nl] = y; nl++;
        font_text_clip(cv, &FONT_SMALL, x, y, net_name(c), col, 255, 64);
        float d = hypotf(mv->ax[k] - mv->px[mv->T - 1], mv->ay[k] - mv->py[mv->T - 1]);
        snprintf(buf, sizeof buf, "%s~%d M", mv->ok[k] ? "" : "? ", (int)lroundf(d));
        micro_text(cv, x, y + 10, buf, C_DIM, 220, 1);
    }

    // you: arrow if heading is known, else a dot
    if (mv->T) {
        float sx, sy;
        to_screen(&f, mv->px[mv->T - 1], mv->py[mv->T - 1], &sx, &sy);
        gfx_glow(cv, sx, sy, 12.0f, C_FOCUS, 120);
        if (mv->heading_ok) {
            float h = mv->heading * D2R, s = sinf(h), c = cosf(h);
            gfx_tri(cv, sx + 8 * s, sy - 8 * c, sx - 5 * c - 4 * s, sy - 5 * s + 4 * c,
                    sx + 5 * c - 4 * s, sy + 5 * s + 4 * c, C_FOCUS, 255);
        } else {
            gfx_disc(cv, sx, sy, 3.5f, C_FOCUS, 255);
        }
    }

    // scale bar + notes
#if RADAR_ROUND
    {
        int len = (int)(step * f.s), x0 = GFX_W / 2 - len / 2, y = GFX_H - 36;
        gfx_hline(cv, x0, x0 + len, y, C_FOCUS, 230);
        gfx_vline(cv, x0, y - 3, y, C_FOCUS, 230);
        gfx_vline(cv, x0 + len, y - 3, y, C_FOCUS, 230);
        snprintf(buf, sizeof buf, "%d M", (int)step);
        micro_text(cv, GFX_W / 2 - micro_width(buf, 1) / 2, y + 4, buf, C_FOCUS, 230, 1);
    }
#else
    gfx_hline(cv, 8, 8 + (int)(step * f.s), MAP_BOT - 4, C_FOCUS, 230);
    gfx_vline(cv, 8, MAP_BOT - 7, MAP_BOT - 4, C_FOCUS, 230);
    gfx_vline(cv, 8 + (int)(step * f.s), MAP_BOT - 7, MAP_BOT - 4, C_FOCUS, 230);
    snprintf(buf, sizeof buf, "%d M", (int)step);
    micro_text(cv, 12 + (int)(step * f.s), MAP_BOT - 8, buf, C_FOCUS, 230, 1);
    const char *up = mv->aligned ? "UP = WHERE YOU FACED AT THE TURN" : "UP = YOUR FIRST STEPS   L/R ?";
    micro_text(cv, GFX_W - 6 - micro_width(up, 1), MAP_BOT - 8, up, C_DIM, 220, 1);
#endif

    if (!mv->active) {
        const int bw = RADAR_ROUND ? 200 : 220, bx = GFX_W / 2 - bw / 2, by = GFX_H / 2 - 50;
        chamfer_box(cv, bx, by, bw, 74, C_BG0, 210, C_FOCUS, 220);
        micro_text(cv, bx + 12, by + 10, "WALK SLOWLY THROUGH", C_FOCUS, 245, 2);
        micro_text(cv, bx + 12, by + 26, "EVERY ROOM", C_FOCUS, 245, 2);
        snprintf(buf, sizeof buf, "THE MAP NEEDS 3 M OF WALK: %.1f", (double)mv->spread);
        micro_text(cv, bx + 12, by + 48, buf, C_DIM, 230, 1);
        micro_text(cv, bx + 12, by + 58, "A 360 TURN THEN FIXES LEFT/RIGHT", C_DIM, 230, 1);
    }

#if RADAR_EPAPER
    key_hints(cv, 4, GFX_H - 34, GFX_W - 8, 30, "BACK = RADAR    MENU = NEXT SCREEN    DIAL OK = NEW WALK", "HOLD MENU = 360 TURN");
#elif !RADAR_ROUND
    const int bw = (GFX_W - 20) / 3;
    button(cv, 4, GFX_H - 34, bw, 30, "NEW WALK", 0);
    button(cv, 4 + (bw + 6), GFX_H - 34, bw, 30, "360 TURN", 0);
    button(cv, 4 + 2 * (bw + 6), GFX_H - 34, bw, 30, "BACK", 0);
#endif
    snprintf(buf, sizeof buf, "%02u:%02u  %d NETS", (unsigned)(mv->seconds / 60u), (unsigned)(mv->seconds % 60u), mv->n);
    view_header(cv, "WALK MAP", buf);
}

/* --- touch calibration screen, logic is cal_touch() in ui.c --- */

void view_cal(ui_t *u, canvas_t *cv)
{
    char buf[32];
    int s = u->cal_step < CAL_POINTS ? u->cal_step : CAL_POINTS - 1;
    float x = (float)CAL_PT[s][0], y = (float)CAL_PT[s][1];
    float f = (float)(u->now % 1000u) / 1000.0f;     // pulse ring, once a second
    gfx_ring(cv, x, y, 6.0f + 14.0f * f, 1.2f, C_FOCUS, (int)(200 * (1.0f - f)));
    gfx_line(cv, x - 14, y, x + 14, y, 1.2f, C_FOCUS, 240);
    gfx_line(cv, x, y - 14, x, y + 14, 1.2f, C_FOCUS, 240);
    gfx_ring(cv, x, y, 6.0f, 1.2f, C_PRIM, 240);
    if (u->t_down) gfx_disc(cv, x, y, 5.0f, C_FOCUS, 200);

    int ty = CAL_PT[s][1] <= GFX_H / 2 ? GFX_H / 2 + 26 : GFX_H / 2 - 66;   // text goes to the other half so it wont cover the cross
    const char *t1 = "TOUCH ALIGNMENT";
    micro_text(cv, GFX_W / 2 - micro_width(t1, 2) / 2, ty, t1, C_FOCUS, 250, 2);
    const char *t2 = "PRESS AND HOLD THE CENTRE OF THE CROSS";
    micro_text(cv, GFX_W / 2 - micro_width(t2, 1) / 2, ty + 20, t2, C_DIM, 235, 1);
    const char *t3 = "WITH A FINGERNAIL OR A STYLUS";
    micro_text(cv, GFX_W / 2 - micro_width(t3, 1) / 2, ty + 29, t3, C_DIM, 235, 1);
    snprintf(buf, sizeof buf, "POINT %d OF %d", s + 1, CAL_POINTS);
    micro_text(cv, GFX_W / 2 - micro_width(buf, 1) / 2, ty + 44, buf, C_PRIM, 240, 1);
    const char *t4 = "BOOT BUTTON: SKIP";
    micro_text(cv, GFX_W / 2 - micro_width(t4, 1) / 2, ty + 58, t4, C_QUIET, 230, 1);
}
