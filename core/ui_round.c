/* round glass (GC9A01 240x240): the main view is the radar itself, target and list
 * are laid out for a circle. nothing may sit in the corners, they are not there.
 * spectrum and walk map are the normal views with round margins (ui_views.c) */
#include "ui_int.h"

#if RADAR_ROUND

#define CX   (GFX_W / 2)
#define CY   (GFX_H / 2)
#define SAFE (GFX_W / 2 - 6)        // the bezel eats a few px, ViewOwl keeps to 112 on 240

// half width of the glass at row y, minus a margin
static int chord(int y, int margin)
{
    int dy = y - CY;
    int h2 = SAFE * SAFE - dy * dy;
    return h2 > 0 ? (int)sqrtf((float)h2) - margin : 0;
}

static void centre_micro(canvas_t *cv, int y, const char *s, uint32_t col, int a)
{
    micro_text(cv, CX - micro_width(s, 1) / 2, y, s, col, a, 1);
}

// little box under the radar centre, one or two lines
static void note_box(canvas_t *cv, const char *big, const char *small, uint32_t col)
{
    int wb = big ? font_width(&FONT_SMALL, big) : 0, ws = small ? micro_width(small, 1) : 0;
    int w = (wb > ws ? wb : ws) + 12, y = CY + 50, h = big && small ? 24 : 15;
    if (w > 2 * chord(y + h, 4)) w = 2 * chord(y + h, 4);
    gfx_fill(cv, CX - w / 2, y, w, h, C_BG0, 170);
    gfx_hline(cv, CX - w / 2, CX + w / 2 - 1, y, col, 150);
    if (big) font_text_clip(cv, &FONT_SMALL, CX - wb / 2, y + 2, big, col, 255, w - 8);
    if (small) centre_micro(cv, big ? y + 15 : y + 5, small, C_DIM, 230);
}

void round_main(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char big[48], small[40];
    radar_draw(u, cv, &G_MAIN, 1);
    ui_lap(u, 2);
    if (!gfx_touches(cv, CY + 48, CY + 76)) return;
    if (a->sweep == SWEEP_COUNTDOWN) {
        note_box(cv, "FACE FORWARD", "THEN TURN RIGHT WITH THE BEAM", C_FOCUS);
    } else if (a->sweep == SWEEP_RUN) {
        uint32_t el = u->now - a->sweep_t0;
        snprintf(small, sizeof small, "KEEP TURNING  %u S", (unsigned)((SWEEP_RUN_MS - el + 999u) / 1000u));
        note_box(cv, 0, small, C_FOCUS);
    } else if (a->sweep == SWEEP_RESULT) {
        snprintf(big, sizeof big, "%d/%d FOUND", a->sweep_resolved, a->sweep_total);
        note_box(cv, big, "UP = WHERE YOU FACED", C_FOCUS);
    } else if (u->target >= 0) {
        const contact_t *c = &a->c[u->target];
        snprintf(big, sizeof big, "%s", net_name(c));
        snprintf(small, sizeof small, "%d DBM  ~%d M", (int)lroundf(c->rssi), (int)lroundf(app_distance_m(c->rssi)));
        note_box(cv, big, small, C_FOCUS);
    } else if (a->n) {
        snprintf(small, sizeof small, "%d NETS  CH %02d", a->n, a->channel);
        note_box(cv, 0, small, C_PRIM);
    }
}

/* the hunt, for a circle: closeness is the arc around the edge, the number in the middle */
void round_target(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    const contact_t *c = &a->c[u->target];
    const blip_t *b = &u->b[u->target];
    char buf[48];

    // proximity: 26 segments on a 260 degree arc, open at the bottom
    float f = clampf((c->rssi + 92.0f) / 60.0f, 0, 1);
    for (int s = 0; s < 26; s++) {
        float a0 = -130.0f + s * 10.0f;
        int lit = (float)s < f * 26.0f;
        gfx_arc(cv, (float)CX, (float)CY, SAFE - 4.0f, a0 + 1.5f, a0 + 8.5f, 6.0f,
                lit ? (s > 19 ? C_FOCUS : C_PRIM) : C_QUIET, lit ? 230 : 60);
    }
    micro_text(cv, CX - 76, CY + 78, "FAR", C_DIM, 220, 1);
    micro_text(cv, CX + 76 - micro_width("NEAR", 1), CY + 78, "NEAR", C_DIM, 220, 1);

    int w = font_width(&FONT_MEDIUM, net_name(c));
    if (w > 150) w = 150;
    font_text_clip(cv, &FONT_MEDIUM, CX - w / 2, 26, net_name(c), C_FOCUS, 255, 150);
    snprintf(buf, sizeof buf, "CH %d  %s", c->channel, sec_name(c->sec));
    centre_micro(cv, 42, buf, sec_color(c->sec), 230);

    snprintf(buf, sizeof buf, "%d", (int)lroundf(c->rssi));
    w = font_width(&FONT_HUGE, buf);
    font_text(cv, &FONT_HUGE, CX - w / 2 - 8, 50, buf, C_FOCUS, 255);
    micro_text(cv, CX + w / 2 - 4, 80, "DBM", C_DIM, 230, 1);

    float tr = app_trend(c);
    const char *word = tr > TREND_DB ? "WARMER" : tr < -TREND_DB ? "COLDER" : "STEADY";
    uint32_t wc = tr > TREND_DB ? C_FOCUS : C_DIM;
    w = font_width(&FONT_MEDIUM, word) + 20;
    int x = CX - w / 2;
    if (tr > TREND_DB) gfx_tri(cv, (float)x, 112, x + 14.0f, 112, x + 7.0f, 100, wc, 240);
    else if (tr < -TREND_DB) gfx_tri(cv, (float)x, 100, x + 14.0f, 100, x + 7.0f, 112, wc, 240);
    else gfx_fill(cv, x, 105, 14, 3, wc, 230);
    font_text(cv, &FONT_MEDIUM, x + 20, 98, word, wc, 255);

    float d = app_distance_m(c->rssi);
    if (d < 10.0f) snprintf(buf, sizeof buf, "~%.1f M", (double)d);
    else snprintf(buf, sizeof buf, "~%d M", (int)lroundf(d));
    if (b->src) {
        int n = (int)strlen(buf);
        snprintf(buf + n, sizeof buf - (size_t)n, "  BRG %03d", (int)lroundf(b->ang) % 360);
    }
    centre_micro(cv, 118, buf, C_FOCUS, 240);

    draw_sparkline(cv, c, CX - 70, 132, 140, 34, 0);

    // which way: small dial at the bottom, "?" when no bearing yet
    const float dx = (float)CX, dy = CY + 64.0f, r = 11.0f;
    gfx_ring(cv, dx, dy, r, 1.2f, C_QUIET, 220);
    if (b->src) {
        float th = b->ang * D2R;
        gfx_line(cv, dx, dy, dx + (r - 2) * sinf(th), dy - (r - 2) * cosf(th), 2.0f, C_FOCUS, 240);
    } else {
        micro_text(cv, CX - 1, (int)dy - 2, "?", C_DIM, 230, 1);
    }
}

/* list: same rows as the square screen, each one as wide as the glass is at that height */
void round_list(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char buf[16];
    for (int k = 0; k < a->n; k++) {
        int ry = LIST_Y0 + k * ROW_H - u->scroll;
        if (ry >= LIST_Y1) break;
        if (ry + ROW_H <= LIST_Y0 || !gfx_touches(cv, ry, ry + ROW_H)) continue;
        int i = u->list[k];
        const contact_t *c = &a->c[i];
        const blip_t *b = &u->b[i];
        // the narrower end of the row decides how wide it can be
        int h1 = chord(ry, 4), h2 = chord(ry + ROW_H - 1, 4), half = h1 < h2 ? h1 : h2;
        int x0 = CX - half, x1 = CX + half;
        if (i == u->target) gfx_fill(cv, x0, ry, x1 - x0, ROW_H - 1, C_FOCUS, 30);
        gfx_fill(cv, x0, ry + 3, 2, ROW_H - 7, b->col, 230);
        snprintf(buf, sizeof buf, "%d", (int)lroundf(c->rssi));
        int dw = font_width(&FONT_MEDIUM, buf);
        font_text(cv, &FONT_MEDIUM, x1 - dw, ry + 3, buf, C_FOCUS, 255);
        font_text_clip(cv, &FONT_MEDIUM, x0 + 6, ry + 3, net_name(c), gfx_mix(b->col, C_FOCUS, 120), 255, x1 - x0 - dw - 12);
        gfx_hline(cv, x0, x1, ry + ROW_H - 1, C_QUIET, 45);
    }
    snprintf(buf, sizeof buf, "%d", a->n);
    view_header(cv, "NETWORKS", buf);
    if (a->n * ROW_H > LIST_Y1 - LIST_Y0) {          // more below or above: small arrows
        if (u->scroll > 0) arrow(cv, (float)CX, LIST_Y0 - 3.0f, 1, C_DIM);
        if (u->scroll < a->n * ROW_H - (LIST_Y1 - LIST_Y0)) arrow(cv, (float)CX, LIST_Y1 + 5.0f, 0, C_DIM);
    }
}

#endif
