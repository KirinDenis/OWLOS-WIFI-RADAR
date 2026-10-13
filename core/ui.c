/* ui: colour themes, per-frame layout, main view, touch input.
 * other full screen views are in ui_views.c */
#include "ui_int.h"
#include "paper.h"
#include <stdlib.h>

const theme_t UI_THEME[UI_THEMES] = {
    /* name        bg0              bg1              glass             panel
                   prim             quiet            focus             dim              open             weak */
#if RADAR_EPAPER
    /* paper prints the negative (paper.c): black here is white paper, white is ink.
     * same 4 tones as ViewOwl's plates: ink for all that is read, the panel's own dark
     * grey for the quiet things, its light grey for glass and bands. security colours make
     * no sense in grey, the list says it in words anyway */
    { "PAPER",    RGB(0, 0, 0),    RGB(0, 0, 0),    INK_GL,           INK_GL,
                  INK_BK,          INK_GD,          INK_BK,           INK_GD,          INK_BK,          INK_BK },
#else
    { "ARCTIC",  RGB(3, 8, 18),   RGB(9, 22, 46),  RGB(16, 42, 82),  RGB(8, 26, 54),
                  RGB(92, 225, 255), RGB(46, 130, 178), RGB(234, 247, 255), RGB(120, 160, 196), RGB(255, 184, 72), RGB(255, 96, 96) },
    { "PHOSPHOR", RGB(2, 9, 4),    RGB(6, 24, 10),  RGB(10, 46, 20),  RGB(6, 28, 12),
                  RGB(90, 255, 130), RGB(40, 150, 70), RGB(225, 255, 230), RGB(130, 190, 145), RGB(255, 200, 70), RGB(255, 90, 80) },
    { "AMBER",    RGB(10, 5, 0),   RGB(26, 14, 2),  RGB(46, 26, 4),   RGB(28, 16, 2),
                  RGB(255, 180, 40), RGB(160, 100, 20), RGB(255, 244, 214), RGB(200, 160, 110), RGB(110, 215, 255), RGB(255, 80, 70) },
    { "CRIMSON",  RGB(10, 1, 4),   RGB(26, 5, 10),  RGB(46, 10, 18),  RGB(28, 6, 12),
                  RGB(255, 80, 100), RGB(160, 40, 60), RGB(255, 226, 230), RGB(205, 140, 150), RGB(255, 205, 90), RGB(200, 140, 255) },
    { "SYNTH",    RGB(6, 2, 16),   RGB(20, 6, 38),  RGB(34, 12, 64),  RGB(20, 8, 40),
                  RGB(205, 125, 255), RGB(110, 60, 170), RGB(250, 238, 255), RGB(170, 145, 205), RGB(255, 190, 80), RGB(255, 90, 130) },
#endif
};
const theme_t *TH = &UI_THEME[0];
uint32_t (*ui_clock_us)(void);
static uint32_t s_lap;          // for ui_lap() profiling

// adds time since last lap to prof[layer]
void ui_lap(ui_t *u, int layer)
{
    if (!ui_clock_us) return;
    uint32_t t = ui_clock_us();
    u->prof[layer] += t - s_lap;
    s_lap = t;
}

int CAL_PT[CAL_POINTS][2];   // calibration crosses, screen px, last one is centre. set in ui_init

/* --- helpers --- */

uint32_t sec_color(int sec)
{
    if (sec == SEC_OPEN) return C_OPEN;
    if (sec == SEC_WEP || sec == SEC_WPA) return C_WEAK;
    return C_PRIM;
}

const char *sec_name(int sec)
{
    static const char *n[] = { "OPEN", "WEP", "WPA", "WPA2", "WPA3" };
    return sec <= SEC_WPA3 ? n[sec] : "?";
}

const char *net_name(const contact_t *c)
{
    return (c->hidden || !c->ssid[0]) ? "[hidden]" : c->ssid;
}

float compass_deg(float dx, float dy)
{
    return wrap360(atan2f(dx, dy) / D2R);
}

void arrow(canvas_t *cv, float x, float y, int up, uint32_t c)
{
    if (up) gfx_tri(cv, x - 3, y + 2, x + 3, y + 2, x, y - 3, c, 240);
    else gfx_tri(cv, x - 3, y - 2, x + 3, y - 2, x, y + 3, c, 240);
}

void chamfer_box(canvas_t *cv, int x, int y, int w, int h, uint32_t fill, int fill_a, uint32_t line, int line_a)
{
    if (!gfx_touches(cv, y - 1, y + h)) return;
    float x0 = (float)x, y0 = (float)y, x1 = (float)(x + w - 1), y1 = (float)(y + h - 1), k = 5.0f;
    if (fill_a) gfx_fill(cv, x, y, w, h, fill, fill_a);
    gfx_line(cv, x0 + k, y0, x1, y0, 1.0f, line, line_a);
    gfx_line(cv, x1, y0, x1, y1 - k, 1.0f, line, line_a);
    gfx_line(cv, x1, y1 - k, x1 - k, y1, 1.0f, line, line_a);
    gfx_line(cv, x1 - k, y1, x0, y1, 1.0f, line, line_a);
    gfx_line(cv, x0, y1, x0, y0 + k, 1.0f, line, line_a);
    gfx_line(cv, x0, y0 + k, x0 + k, y0, 1.0f, line, line_a);
}

void button(canvas_t *cv, int x, int y, int w, int h, const char *label, int lit)
{
    chamfer_box(cv, x, y, w, h, lit ? C_PRIM : C_PANEL, lit ? 90 : 170, lit ? C_FOCUS : C_PRIM, 220);
    int lw = micro_width(label, 1);
    micro_text(cv, x + (w - lw) / 2, y + (h - 5) / 2, label, lit ? C_FOCUS : C_PRIM, 250, 1);
}

void view_header(canvas_t *cv, const char *title, const char *right)
{
#if RADAR_ROUND
    // round glass: the top band is mostly not there, so title and note go centred, lower down
    if (!gfx_touches(cv, 10, 34)) return;
    int tw = font_width(&FONT_SMALL, title);
    gfx_tri(cv, GFX_W / 2.0f - tw / 2 - 9, 15.5f, GFX_W / 2.0f - tw / 2 - 4, 11.0f, GFX_W / 2.0f - tw / 2 - 4, 20.0f, C_FOCUS, 240);
    font_text(cv, &FONT_SMALL, GFX_W / 2 - tw / 2, 10, title, C_FOCUS, 255);
    if (right) micro_text(cv, GFX_W / 2 - micro_width(right, 1) / 2, 24, right, C_DIM, 235, 1);
    gfx_hline(cv, GFX_W / 2 - 60, GFX_W / 2 + 60, 33, C_QUIET, 160);
#else
    if (!gfx_touches(cv, 0, 17)) return;
    gfx_fill(cv, 0, 0, GFX_W, 17, C_PANEL, 210);
    gfx_hline(cv, 0, GFX_W - 1, 17, C_QUIET, 220);
    gfx_tri(cv, 9.0f, 4.0f, 9.0f, 13.0f, 4.0f, 8.5f, C_FOCUS, 240);
    font_text(cv, &FONT_SMALL, 14, 3, title, C_FOCUS, 255);
    int tw = font_width(&FONT_SMALL, title);
    if (right) {
        int rw = micro_width(right, 1);
        micro_text(cv, GFX_W - 6 - rw, 6, right, C_DIM, 235, 1);
        gfx_hatch(cv, 14 + tw + 6, 6, GFX_W - 12 - rw - (14 + tw + 6), 5, C_QUIET, 110);
    } else {
        gfx_hatch(cv, 14 + tw + 6, 6, GFX_W - 6 - (14 + tw + 6), 5, C_QUIET, 110);
    }
#endif
}

/* --- background: gradient, faint grid and dust, rebuilt on theme change --- */

static uint32_t s_bgrow[GFX_H];
static uint16_t s_vigx[GFX_W], s_vigy[GFX_H];
static int16_t  s_gridcols[GFX_W / 12 + 2];
static int      s_ngrid;
static struct { int16_t x, y; uint8_t a; } s_stars[128];
static int      s_nstars, s_bg_theme = -1;

/* per-pixel vignette cost 14 ms a frame on the board. now a row is one colour,
 * grid is just a few columns on top, and only outer VIG_W px each side get darker */
#define VIG_W 56

static void build_background(void)
{
    for (int y = 0; y < GFX_H; y++) {
        float t = (float)y / (GFX_H - 1);
        s_bgrow[y] = gfx_mix(C_BG0, C_BG1, (uint32_t)(256.0f * (0.25f + 0.75f * t)));
        float v = ((float)y - GFX_H / 2.0f) / (GFX_H * 7.0f / 12.0f);    // 140 on a 240 high screen
        s_vigy[y] = (uint16_t)(256.0f * clampf(1.0f - 0.45f * v * v, 0.3f, 1.0f));
    }
    s_ngrid = 0;
    for (int x = 0; x < GFX_W; x++) {
        int e = x < GFX_W / 2 ? x : GFX_W - 1 - x;           // distance to nearest edge
        float v = e < VIG_W ? 1.0f - (float)e / VIG_W : 0.0f;
        s_vigx[x] = (uint16_t)(256.0f * (1.0f - 0.4f * v * v));
        if (((x - G_MAIN.cx + 1200) % 12) == 0) s_gridcols[s_ngrid++] = (int16_t)x;   // line through radar centre, +1200 keeps % positive
    }
    s_nstars = 0;
    for (int y = 0; y < GFX_H && !RADAR_EPAPER; y++)   // faint dust specks, hashed from x,y so they dont move. on paper they'd just look like dirt
        for (int x = 0; x < GFX_W && s_nstars < 128; x++) {
            uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            if ((h & 1023u) == 0) {
                s_stars[s_nstars].x = (int16_t)x;
                s_stars[s_nstars].y = (int16_t)y;
                s_stars[s_nstars].a = (uint8_t)(30 + ((h >> 10) & 127));
                s_nstars++;
            }
        }
}

CORE_HOT void draw_background(canvas_t *cv)
{
    if (RADAR_EPAPER) {                               // paper: clean white sheet, a grid would only print as dots
        memset(cv->px, 0, sizeof(uint32_t) * GFX_W * (size_t)cv->h);
        return;
    }
    const uint32_t grid = gfx_scale(C_QUIET, 40), node = gfx_scale(C_QUIET, 90);
    for (int r = 0; r < cv->h; r++) {
        int y = cv->y0 + r;
        uint32_t *row = cv->px + r * GFX_W;
        uint32_t base = gfx_scale(s_bgrow[y], s_vigy[y]);
        int lined = (y % 12) == 0;
        uint32_t fill = lined ? gfx_add(base, grid) : base;
        uint32_t col = gfx_add(base, lined ? node : grid);
        for (int x = 0; x < GFX_W; x++) row[x] = fill;
        for (int i = 0; i < s_ngrid; i++) row[s_gridcols[i]] = col;
        for (int x = 0; x < VIG_W; x++) {
            row[x] = gfx_scale(row[x], s_vigx[x]);
            row[GFX_W - 1 - x] = gfx_scale(row[GFX_W - 1 - x], s_vigx[GFX_W - 1 - x]);
        }
    }
    for (int i = 0; i < s_nstars; i++) {
        uint32_t *row = gfx_row(cv, s_stars[i].y);
        if (row) row[s_stars[i].x] = gfx_add(row[s_stars[i].x], gfx_scale(C_DIM, s_stars[i].a));
    }
}

void ui_set_theme(ui_t *u, int theme)
{
    u->theme = ((theme % UI_THEMES) + UI_THEMES) % UI_THEMES;
    TH = &UI_THEME[u->theme];       // careful: TH is global, all the C_* colours read from it
    if (s_bg_theme != u->theme) { build_background(); s_bg_theme = u->theme; }
}

int ui_init(ui_t *u, uint32_t now)
{
    memset(u, 0, sizeof *u);
    u->boot = now;
    u->target = -1;
    u->chan_sel = -1;
    /* default cal untill the user runs calibration. measured on ESP32-2432S028R, MADCTL 0xE8:
     * touch axes are swapped and both inverted (screen x = raw y, screen y = raw x).
     * corners read (3320,3393) (3377,485) (627,401) (618,3384), centre (1980,1955) */
    u->cal = (touch_cal_t){ 0.0f, -0.0894f, 333.3f, -0.0666f, 0.0f, 251.1f };
    if (!radar_init()) return 0;
    // crosses 30 px in from the corners + centre. round glass: corners would be off the
    // glass, so put them on the diagonals instead
    int in = RADAR_ROUND ? (int)(GFX_W * 0.2f) : 30;
    const int pts[CAL_POINTS][2] = { { in, in }, { GFX_W - in, in }, { GFX_W - in, GFX_H - in },
                                     { in, GFX_H - in }, { GFX_W / 2, GFX_H / 2 } };
    memcpy(CAL_PT, pts, sizeof CAL_PT);
    ui_set_theme(u, 0);
    return 1;
}

/* --- per-frame layout --- */

static anim_t *anim_for(ui_t *u, uint32_t id, uint32_t now, int *fresh)
{
    anim_t *free_slot = 0;
    for (int i = 0; i < MAX_CONTACTS; i++) {
        if (u->anim[i].id == id) { *fresh = 0; return &u->anim[i]; }
        if (!u->anim[i].id && !free_slot) free_slot = &u->anim[i];
    }
    if (!free_slot) free_slot = &u->anim[0];    // hack: table full, just reuse slot 0
    memset(free_slot, 0, sizeof *free_slot);
    free_slot->id = id;
    free_slot->born = now;
    *fresh = 1;
    return free_slot;
}

/* network with no direction yet: fixed angle from FNV hash of the BSSID, drawn hollow.
 * used to orbit slowly, but a circling dot looks like movement that isnt there */
static float ghost_angle(const uint8_t *m)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) h = (h ^ m[i]) * 16777619u;
    return (float)(h % 3600u) * 0.1f;
}

/* bearing for one network: from walk map (relative to your position and walking
 * direction), from last 360 turn (only while you still stand there), both fused, or none.
 * src: bit 0 = turn, bit 1 = map */
static void direction(const ui_t *u, const contact_t *c, int turn_fresh, float *ang, float *sd, uint8_t *src)
{
    const mapview_t *mv = &u->snap.map;
    float ma = 0, msd = 0;
    int mok = 0;
    if (mv->active && mv->T > 0) {
        // ahead = walking direction, or map's up until heading is known
        float hd = mv->heading_ok ? mv->heading : 0.0f;
        for (int k = 0; k < mv->n; k++) {
            if (mv->id[k] != c->id || !mv->ok[k]) continue;
            float dx = mv->ax[k] - mv->px[mv->T - 1], dy = mv->ay[k] - mv->py[mv->T - 1];
            float d = sqrtf(dx * dx + dy * dy);
            ma = wrap360(compass_deg(dx, dy) - hd);
            msd = atan2f(mv->sd[k], d) / D2R + (mv->heading_ok ? 15.0f : 25.0f);   // + slack for the heading itself
            mok = 1;
        }
    }
    int tok = turn_fresh && c->has_bearing;
    if (mok && tok && !mv->aligned) mok = 0;              // map and turn frames not aligned, use the turn only
    *src = (uint8_t)(tok | (mok << 1));
    if (mok && tok) {               // inverse-variance weights, summed as vectors so 359/1 deg works
        float w1 = 1.0f / (msd * msd), w2 = 1.0f / (c->bearing_sd * c->bearing_sd);
        float x = w1 * cosf(ma * D2R) + w2 * cosf(c->bearing * D2R);
        float y = w1 * sinf(ma * D2R) + w2 * sinf(c->bearing * D2R);
        *ang = wrap360(atan2f(y, x) / D2R);
        *sd = 1.0f / sqrtf(w1 + w2);
    } else if (mok) {
        *ang = ma; *sd = msd;
    } else if (tok) {
        *ang = c->bearing; *sd = c->bearing_sd;
    }
}

// sort 0 = signal (u->order is already loudest first), 1 = name, 2 = channel then signal
void list_order(ui_t *u)
{
    const app_t *a = &u->snap;
    int n = a->n;
    for (int i = 0; i < n; i++) u->list[i] = u->order[i];
    if (u->sort == 0) return;
    for (int i = 1; i < n; i++) {
        uint8_t v = u->list[i];
        int j = i - 1;
        while (j >= 0) {
            const contact_t *p = &a->c[u->list[j]], *q = &a->c[v];
            int after = u->sort == 1 ? strcmp(net_name(p), net_name(q)) > 0
                                     : (p->channel > q->channel || (p->channel == q->channel && p->rssi < q->rssi));
            if (!after) break;
            u->list[j + 1] = u->list[j];
            j--;
        }
        u->list[j + 1] = v;
    }
}

// once per frame, before the strips: blips, directions, spectrum, layout
void ui_frame(ui_t *u, uint32_t now)
{
    app_t *a = &u->snap;
    ui_set_theme(u, u->theme);
    u->now = now;
    u->beam = a->sweep == SWEEP_RUN ? app_heading(a, now) : (float)(now % 4000u) * (360.0f / 4000.0f);
    u->n = app_sorted(a, u->order);
    u->target = -1;
    for (int i = 0; i < a->n; i++) if (a->c[i].id == a->target_id) u->target = i;
    if (u->target < 0 && u->view == VIEW_TARGET) u->view = VIEW_MAIN;

    for (int s = 0; s < MAX_CONTACTS; s++) {      // free anim slots of networks that are gone
        if (!u->anim[s].id) continue;
        int alive = 0;
        for (int i = 0; i < a->n && !alive; i++) alive = a->c[i].id == u->anim[s].id;
        if (!alive) u->anim[s].id = 0;
    }

    const mapview_t *mv = &a->map;
    int turn_fresh = a->bearing_ms && (!mv->active || !mv->turned || mv->since_turn < 2.5f);
    int used_map = 0, used_turn = 0;
    memset(u->spectrum, 0, sizeof u->spectrum);
    for (int i = 0; i < a->n; i++) {
        const contact_t *c = &a->c[i];
        blip_t *b = &u->b[i];
        int fresh;
        anim_t *an = anim_for(u, c->id, now, &fresh);
        float ft = rssi_frac(c->rssi), ga = ghost_angle(c->bssid), ang = 0, sd = 0;
        uint8_t src = 0;
        direction(u, c, turn_fresh, &ang, &sd, &src);
        if (fresh) { an->t = ft; an->ang = src ? ang : ga; }
        an->t += (ft - an->t) * (RADAR_EPAPER ? 1.0f : 0.18f);    // low-pass, raw values are jumpy. paper draws a frame in seconds, no easing there
        an->ang = src ? wrap360(an->ang + wrap180(ang - an->ang) * (RADAR_EPAPER ? 1.0f : 0.12f)) : ga;
        used_map += src >= 2;
        used_turn += src & 1;

        b->ang = an->ang;
        b->sd = sd;
        b->src = src;
        b->ghost = !src;
        b->col = sec_color(c->sec);
        b->sel = i == u->target;
        b->dim = u->target >= 0 && !b->sel;
        b->age_ms = (int)(now - an->born);
        if (RADAR_EPAPER) {
            b->bright = 1.0f;                       // no beam on paper, a half faded dot would only look random
        } else if (a->sweep == SWEEP_RUN || a->sweep == SWEEP_COUNTDOWN) {
            b->bright = 0.8f;
        } else {
            float f = 1.0f - wrap360(u->beam - b->ang) / 360.0f;    // fades after beam passes, like phosphor
            b->bright = 0.35f + 0.65f * f * f;
        }
        int32_t quiet = (int32_t)(now - c->last_ms);
        // not heard for 8 s: fade over next 30 s, down to 25%
        if (quiet > 8000) b->bright *= clampf(1.0f - (float)(quiet - 8000) / 30000.0f, 0.25f, 1.0f);
        snprintf(b->name, sizeof b->name, "%s", net_name(c));
        snprintf(b->sub, sizeof b->sub, "%d CH%d", (int)lroundf(c->rssi), c->channel);

        float v = clampf((c->rssi + 95.0f) / 60.0f, 0, 1);      // 20 MHz channel spills into +-2 neighbours
        for (int d = -2; d <= 2; d++) {
            int ch = c->channel + d;
            if (ch < 1 || ch > 13) continue;
            u->spectrum[ch] += v * (d == 0 ? 0.55f : (d == 1 || d == -1) ? 0.3f : 0.12f);
        }
    }
    for (int ch = 0; ch < 14; ch++) u->spectrum[ch] = clampf(u->spectrum[ch], 0, 1);
    u->up_note = used_turn && turn_fresh ? "UP = TURN START" : used_map ? (mv->heading_ok ? "UP = WALKING" : "UP = MAP UP") : "UP = ?";

    if (u->view == VIEW_RADAR) radar_place(u, &G_FULL);
    else radar_place(u, &G_MAIN);
    if (u->view == VIEW_LIST) list_order(u);
}

/* --- main view: radar on the left, panel on the right --- */

static void bracket(canvas_t *cv, float x, float y, float sx, float sy)
{
    gfx_line(cv, x, y + sy * 20, x, y + sy * 6, 1.2f, C_PRIM, 170);
    gfx_line(cv, x, y + sy * 6, x + sx * 6, y, 1.2f, C_PRIM, 170);
    gfx_line(cv, x + sx * 6, y, x + sx * 20, y, 1.2f, C_PRIM, 170);
}

static void main_decor(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char buf[40];
    bracket(cv, 3, 3, 1, 1);
    bracket(cv, (float)(PX - 6), 3, -1, 1);
    bracket(cv, 3, GFX_H - 4, 1, -1);
    bracket(cv, (float)(PX - 6), GFX_H - 4, -1, -1);
    gfx_hatch(cv, G_MAIN.cx - 31, 3, 62, 4, C_QUIET, 120);
    gfx_hatch(cv, G_MAIN.cx - 31, GFX_H - 7, 62, 4, C_QUIET, 120);

    micro_text(cv, 9, 10, "2.4 GHZ PASSIVE", C_QUIET, 230, 1);
    snprintf(buf, sizeof buf, "CH %02d  %d/S", a->channel, a->pkt_rate);
    micro_text(cv, 9, 17, buf, C_DIM, 230, 1);
    snprintf(buf, sizeof buf, "NETS %d", a->n);
    micro_text(cv, 9, 24, buf, C_DIM, 230, 1);

    micro_text(cv, PX - 9 - micro_width("RSSI DBM", 1), 10, "RSSI DBM", C_QUIET, 230, 1);
    micro_text(cv, PX - 9 - micro_width(u->up_note, 1), 17, u->up_note, C_DIM, 230, 1);

    const mapview_t *mv = &a->map;
    if (mv->active) {
        snprintf(buf, sizeof buf, "MAP %d NETS %s", mv->n, mv->aligned ? "" : "L/R ?");
        micro_text(cv, 9, GFX_H - 17, buf, C_DIM, 230, 1);
    } else if (a->bearing_ms) {
        uint32_t s = (u->now - a->bearing_ms) / 1000u;
        snprintf(buf, sizeof buf, "BRG %d/%d  %02u:%02u AGO", a->sweep_resolved, a->sweep_total,
                 (unsigned)(s / 60u % 100u), (unsigned)(s % 60u));
        micro_text(cv, 9, GFX_H - 17, buf, C_DIM, 230, 1);
    } else {
        micro_text(cv, 9, GFX_H - 17, "WALK OR TURN 360", ((u->now / 600u) & 1) ? C_FOCUS : C_DIM, 230, 1);
    }
    micro_text(cv, 9, GFX_H - 10, "RANGE = SIGNAL", C_QUIET, 230, 1);
}

static void panel_frame(canvas_t *cv)
{
    chamfer_box(cv, PX, 2, PW, GFX_H - 4, C_PANEL, 120, C_QUIET, 200);
}

static void panel_title(canvas_t *cv, const char *title, const char *right)
{
    micro_text(cv, TX, 7, title, C_FOCUS, 240, 1);
    int w = micro_width(title, 1), rw = right ? micro_width(right, 1) : 0;
    if (right) micro_text(cv, TX2 - rw, 7, right, C_DIM, 230, 1);
    gfx_hatch(cv, TX + w + 4, 7, TX2 - (right ? rw + 4 : 0) - (TX + w + 4), 5, C_QUIET, 130);
    gfx_hline(cv, TX, TX2, 15, C_QUIET, 150);
}

void draw_spectrum_small(ui_t *u, canvas_t *cv, int y, int h)
{
    micro_text(cv, TX, y, "SPECTRUM", C_QUIET, 230, 1);
    micro_text(cv, TX2 - micro_width("CH", 1), y, "CH", C_QUIET, 230, 1);
    int base = y + 9 + h;
    int pitch = (TX2 - TX + 2) / 13;              // 6 px a channel on the 320 wide screen
    for (int ch = 1; ch <= 13; ch++) {
        int bx = TX + (ch - 1) * pitch;
        int bh = (int)lroundf(u->spectrum[ch] * (float)h);
        gfx_fill(cv, bx, base - h, pitch - 1, h, C_QUIET, 40);
        if (bh > 0) {
            gfx_fill(cv, bx, base - bh, pitch - 1, bh, C_PRIM, 150);
            gfx_hline(cv, bx, bx + pitch - 2, base - bh, C_FOCUS, 220);
        }
        if (ch == u->snap.channel) gfx_fill(cv, bx, base + 1, pitch - 1, 2, C_FOCUS, 255);
    }
    micro_text(cv, TX + 1, base + 4, "1", C_DIM, 230, 1);
    micro_text(cv, TX + 5 * pitch + 1, base + 4, "6", C_DIM, 230, 1);
    micro_text(cv, TX + 10 * pitch - 1, base + 4, "11", C_DIM, 230, 1);
    micro_text(cv, TX + 12 * pitch - 1, base + 4, "13", C_DIM, 230, 1);
}

void draw_sparkline(canvas_t *cv, const contact_t *c, int x, int y, int w, int h, int big)
{
    if (!gfx_touches(cv, y, y + h)) return;
    gfx_fill(cv, x, y, w, h, C_BG0, 120);
    for (int db = -90; db <= -40; db += big ? 10 : 25) {
        int gy = y + h - 1 - (int)((float)(db + 95) / 65.0f * (h - 1));
        gfx_hline(cv, x, x + w - 1, gy, C_QUIET, (db % 20 == 0 || !big) ? 70 : 35);
        if (big && db % 20 == 0) {
            char buf[16];
            snprintf(buf, sizeof buf, "%d", db);
            micro_text(cv, x + w - micro_width(buf, 1) - 2, gy - 7, buf, C_QUIET, 200, 1);
        }
    }
    float px = 0, py = 0;
    int prev = 0;
    for (int i = 0; i < HIST_LEN; i++) {
        int k = (c->hist_head + i) % HIST_LEN;
        if (c->hist[k] == HIST_NONE) { prev = 0; continue; }
        float vx = x + (float)i * (float)(w - 1) / (HIST_LEN - 1);
        float vy = y + h - 1 - clampf((float)(c->hist[k] + 95) / 65.0f, 0, 1) * (h - 1);
        gfx_vline(cv, (int)lroundf(vx), (int)lroundf(vy), y + h - 1, C_PRIM, big ? 30 : 40);
        if (prev) gfx_line(cv, px, py, vx, vy, big ? 1.6f : 1.2f, C_PRIM, 230);
        px = vx; py = vy; prev = 1;
    }
    if (prev) gfx_disc(cv, px, py, big ? 2.5f : 1.8f, C_FOCUS, 255);
    micro_text(cv, x + 2, y + 2, "60 S", C_DIM, 200, 1);
}

// 3 buttons under the side panel, spectrum above them (320x240: buttons at y 202, 26 px wide)
#define BTN_Y  (GFX_H - 38)
#define BTN_H  32
#define BTN_W  ((PW - 13) / 3)
#define SPEC_Y (GFX_H - 88)
static int btn_x(int i) { return TX - 3 + i * (BTN_W + 3); }

/* e-paper boards have a dial and keys, no touch. where the touch screens have buttons
 * they get a box saying what the keys do */
void key_hints(canvas_t *cv, int x, int y, int w, int h, const char *l1, const char *l2)
{
    chamfer_box(cv, x, y, w, h, C_PANEL, 170, C_QUIET, 220);
    int y1 = l2 ? y + h / 2 - 7 : y + (h - 5) / 2;
    micro_text(cv, x + (w - micro_width(l1, 1)) / 2, y1, l1, C_FOCUS, 250, 1);
    if (l2) micro_text(cv, x + (w - micro_width(l2, 1)) / 2, y1 + 9, l2, C_DIM, 240, 1);
}

static void panel_buttons(ui_t *u, canvas_t *cv, const char *a, const char *b, const char *c)
{
    (void)u;
#if RADAR_EPAPER
    (void)a; (void)b; (void)c;
    key_hints(cv, btn_x(0), BTN_Y, 3 * BTN_W + 6, BTN_H, "DIAL PICK  OK OPEN", "HOLD OK = 360 TURN");
#else
    button(cv, btn_x(0), BTN_Y, BTN_W, BTN_H, a, 0);
    button(cv, btn_x(1), BTN_Y, BTN_W, BTN_H, b, 0);
    button(cv, btn_x(2), BTN_Y, BTN_W, BTN_H, c, 0);
#endif
}

static void panel_overview(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char buf[48];
    panel_title(cv, RADAR_NAME, 0);

    snprintf(buf, sizeof buf, "%d", a->n);
    int w = font_text(cv, &FONT_LARGE, TX - 1, 18, buf, C_FOCUS, 255);
    int open = 0;
    for (int i = 0; i < a->n; i++) open += a->c[i].sec == SEC_OPEN;
    micro_text(cv, TX + w + 4, 24, "NETWORKS", C_DIM, 230, 1);
    snprintf(buf, sizeof buf, "%d OPEN", open);
    micro_text(cv, TX + w + 4, 32, buf, open ? C_OPEN : C_QUIET, 230, 1);

    micro_text(cv, TX, 47, "LOUDEST", C_QUIET, 230, 1);
    micro_text(cv, TX2 - micro_width("ALL >", 1), 47, "ALL >", C_PRIM, 230, 1);
    gfx_hline(cv, TX, TX2, 55, C_QUIET, 110);

    int fit = (SPEC_Y - 2 - 59) / 18;              // rows that fit above the spectrum (5 on 320x240)
    int rows = u->n < fit ? u->n : fit;
    for (int k = 0; k < rows; k++) {
        int i = u->order[k], ry = 59 + k * 18;
        const contact_t *c = &a->c[i];
        const blip_t *b = &u->b[i];
        gfx_vline(cv, PX + 3, ry, ry + 12, b->col, 220);
        font_text_clip(cv, &FONT_SMALL, TX, ry - 1, net_name(c), gfx_mix(b->col, C_FOCUS, 110), 255, PW - 31);
        snprintf(buf, sizeof buf, "%d", (int)lroundf(c->rssi));
        micro_text(cv, TX2 - micro_width(buf, 1), ry + 1, buf, C_FOCUS, 240, 1);
        float tr = app_trend(c);
        if (tr > TREND_DB) arrow(cv, (float)(TX2 - 18), ry + 4.0f, 1, C_FOCUS);
        else if (tr < -TREND_DB) arrow(cv, (float)(TX2 - 18), ry + 4.0f, 0, C_DIM);
        int full = PW - 13, bw = (int)(clampf((c->rssi + 95.0f) / 65.0f, 0, 1) * (float)full);
        gfx_fill(cv, TX, ry + 11, bw, 2, b->col, 210);
        gfx_fill(cv, TX + bw, ry + 11, full - bw, 2, C_QUIET, 50);
    }
    draw_spectrum_small(u, cv, SPEC_Y, 26);
    panel_buttons(u, cv, "360", "MAP", "HUE");
}

// split a long name in two lines by pixel width, utf8 safe. l2 gets the rest, caller clips it
static void split2(const font_t *f, const char *s, int maxw, char *l1, char *l2, int sz)
{
    l2[0] = 0;
    if (font_width(f, s) <= maxw) { strncpy(l1, s, (size_t)sz - 1); l1[sz - 1] = 0; return; }
    const char *p = s;
    int w = 0, n = 0;
    while (*p) {
        const char *q = p;
        utf8_next(&q);
        char one[8] = { 0 };
        memcpy(one, p, (size_t)(q - p));
        int cw = font_width(f, one);
        if (w + cw > maxw || n + (q - p) >= sz - 1) break;
        memcpy(l1 + n, p, (size_t)(q - p));
        n += (int)(q - p);
        w += cw;
        p = q;
    }
    l1[n] = 0;
    strncpy(l2, p, (size_t)sz - 1);
    l2[sz - 1] = 0;
}

static void panel_target(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    const contact_t *c = &a->c[u->target];
    const blip_t *b = &u->b[u->target];
    char buf[48], l1[48], l2[48];
    int rank = 0;
    for (int k = 0; k < u->n; k++) if (u->order[k] == u->target) rank = k + 1;
    snprintf(buf, sizeof buf, "%02d/%02d", rank, u->n);
    panel_title(cv, "TARGET", buf);

    split2(&FONT_MEDIUM, net_name(c), PW - 10, l1, l2, sizeof l1);
    font_text(cv, &FONT_MEDIUM, TX, 18, l1, C_FOCUS, 255);
    if (l2[0]) font_text_clip(cv, &FONT_MEDIUM, TX, 32, l2, C_FOCUS, 255, PW - 10);
    snprintf(buf, sizeof buf, "%02X:%02X:%02X:%02X:%02X:%02X", c->bssid[0], c->bssid[1], c->bssid[2],
             c->bssid[3], c->bssid[4], c->bssid[5]);
    micro_text(cv, TX, 48, buf, C_DIM, 230, 1);

    snprintf(buf, sizeof buf, "%d", (int)lroundf(c->rssi));
    int w = font_text(cv, &FONT_LARGE, TX - 1, 56, buf, C_FOCUS, 255);
    micro_text(cv, TX + w + 3, 74, "DBM", C_DIM, 230, 1);

    float tr = app_trend(c);
    const char *word = tr > TREND_DB ? "WARMER" : tr < -TREND_DB ? "COLDER" : "STEADY";
    if (tr > TREND_DB) arrow(cv, (float)TX + 3, 89.0f, 1, C_FOCUS);
    else if (tr < -TREND_DB) arrow(cv, (float)TX + 3, 89.0f, 0, C_DIM);
    else gfx_fill(cv, TX, 88, 7, 2, C_DIM, 230);
    snprintf(buf, sizeof buf, "%s %+.1f/10S", word, (double)tr);
    micro_text(cv, TX + 10, 86, buf, tr > TREND_DB ? C_FOCUS : C_DIM, 240, 1);

    float d = app_distance_m(c->rssi);
    if (d < 10.0f) snprintf(buf, sizeof buf, "DIST ~%.1f M", (double)d);
    else snprintf(buf, sizeof buf, "DIST ~%d M", (int)lroundf(d));
    micro_text(cv, TX, 96, buf, C_DIM, 230, 1);
    snprintf(buf, sizeof buf, "CH %d  %s", c->channel, sec_name(c->sec));
    micro_text(cv, TX, 104, buf, sec_color(c->sec), 230, 1);
    if (b->src) snprintf(buf, sizeof buf, "BRG %03d* +-%d", (int)lroundf(b->ang) % 360, (int)lroundf(b->sd));
    else snprintf(buf, sizeof buf, "BRG ---");
    micro_text(cv, TX, 112, buf, b->src ? C_FOCUS : C_DIM, 230, 1);

    int dh = GFX_H - 240;                           // taller screens: the extra height goes to the graph
    draw_sparkline(cv, c, TX, 122, TX2 - TX + 1, 40 + dh, 0);
    // proximity bar, so you can walk around without reading numbers
    micro_text(cv, TX, 168 + dh, "PROXIMITY", C_QUIET, 230, 1);
    float f = clampf((c->rssi + 92.0f) / 60.0f, 0, 1);
    int seg = (TX2 - TX + 2) / 13;
    for (int s = 0; s < 13; s++) {
        int lit = (float)s < f * 13.0f;
        gfx_fill(cv, TX + s * seg, 177 + dh, seg - 1, 16, lit ? (s > 9 ? C_FOCUS : C_PRIM) : C_QUIET, lit ? 220 : 45);
    }
    panel_buttons(u, cv, "360", "MAP", "X");
}

static void panel_sweep(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char buf[32];
    panel_title(cv, "360 TURN", 0);
    if (a->sweep == SWEEP_COUNTDOWN) {
        static const char *steps[] = { "STAND IN ONE SPOT", "HOLD IT AT CHEST", "SCREEN TOWARD YOU",
                                       "FACE FORWARD", "THEN TURN RIGHT", "WITH THE BEAM", "ONE TURN: 24 S" };
        for (int k = 0; k < 7; k++)
            micro_text(cv, TX, 24 + k * 9, steps[k], k >= 4 ? C_FOCUS : C_DIM, 235, 1);
    } else if (a->sweep == SWEEP_RUN) {
        uint32_t el = u->now - a->sweep_t0;
        int pct = (int)(el * 100u / SWEEP_RUN_MS);
        snprintf(buf, sizeof buf, "%d%%", pct > 99 ? 99 : pct);
        font_text(cv, &FONT_LARGE, TX - 1, 18, buf, C_FOCUS, 255);
        snprintf(buf, sizeof buf, "%u S LEFT", (unsigned)((SWEEP_RUN_MS - el + 999u) / 1000u));
        micro_text(cv, TX, 48, buf, C_DIM, 230, 1);
        gfx_fill(cv, TX, 58, TX2 - TX + 1, 3, C_QUIET, 60);
        gfx_fill(cv, TX, 58, (TX2 - TX + 1) * pct / 100, 3, C_FOCUS, 230);
        micro_text(cv, TX, 72, "KEEP TURNING", C_FOCUS, 235, 1);
        micro_text(cv, TX, 80, "RIGHT, SLOW AND", C_FOCUS, 235, 1);
        micro_text(cv, TX, 88, "STEADY", C_FOCUS, 235, 1);
        micro_text(cv, TX, 102, "THE LOBE POINTS", C_DIM, 230, 1);
        micro_text(cv, TX, 110, "WHERE IT IS", C_DIM, 230, 1);
    } else {
        snprintf(buf, sizeof buf, "%d/%d", a->sweep_resolved, a->sweep_total);
        font_text(cv, &FONT_LARGE, TX - 1, 18, buf, C_FOCUS, 255);
        micro_text(cv, TX, 48, "BEARINGS FOUND", C_DIM, 230, 1);
        micro_text(cv, TX, 62, "UP ON THE RADAR", C_FOCUS, 235, 1);
        micro_text(cv, TX, 70, "IS WHERE YOU", C_FOCUS, 235, 1);
        micro_text(cv, TX, 78, "FACED AT START", C_FOCUS, 235, 1);
        if (a->map.aligned) {
            micro_text(cv, TX, 92, "WALK MAP NOW", C_DIM, 230, 1);
            micro_text(cv, TX, 100, "KNOWS LEFT/RIGHT", C_DIM, 230, 1);
        }
    }
    draw_spectrum_small(u, cv, SPEC_Y, 26);
    if (RADAR_EPAPER) key_hints(cv, btn_x(0), BTN_Y, 3 * BTN_W + 6, BTN_H, a->sweep == SWEEP_RESULT ? "OK = DONE" : "OK = ABORT", 0);
    else button(cv, btn_x(0), BTN_Y, 3 * BTN_W + 6, BTN_H, a->sweep == SWEEP_RESULT ? "OK" : "ABORT", 0);
}

static void main_view(ui_t *u, canvas_t *cv)
{
    radar_draw(u, cv, &G_MAIN, 0);
    main_decor(u, cv);
    ui_lap(u, 2);
    panel_frame(cv);
    if (u->snap.sweep != SWEEP_IDLE) panel_sweep(u, cv);
    else if (u->target >= 0) panel_target(u, cv);
    else panel_overview(u, cv);
}

/* --- full screen radar --- */

#if !RADAR_ROUND && !RADAR_EPAPER  // round: main already is the full radar. paper: has its own screens
static void radar_view(ui_t *u, canvas_t *cv)
{
    const app_t *a = &u->snap;
    char buf[40];
    radar_draw(u, cv, &G_FULL, 1);
    gfx_tri(cv, 9.0f, 4.0f, 9.0f, 13.0f, 4.0f, 8.5f, C_FOCUS, 240);
    micro_text(cv, 14, 6, "RADAR", C_FOCUS, 240, 1);
    snprintf(buf, sizeof buf, "%d NETS", a->n);
    micro_text(cv, 4, 16, buf, C_DIM, 230, 1);
    snprintf(buf, sizeof buf, "CH %02d %d/S", a->channel, a->pkt_rate);
    micro_text(cv, 4, 24, buf, C_DIM, 230, 1);
    micro_text(cv, GFX_W - 4 - micro_width("RSSI DBM", 1), 6, "RSSI DBM", C_QUIET, 230, 1);
    micro_text(cv, GFX_W - 4 - micro_width(u->up_note, 1), 14, u->up_note, C_DIM, 230, 1);
    if (u->target >= 0) {
        const contact_t *c = &a->c[u->target];
        font_text_clip(cv, &FONT_SMALL, 4, GFX_H - 27, net_name(c), C_FOCUS, 255, 64);
        snprintf(buf, sizeof buf, "%d DBM", (int)lroundf(c->rssi));
        micro_text(cv, 4, GFX_H - 14, buf, C_FOCUS, 240, 1);
    } else {
        micro_text(cv, 4, GFX_H - 14, RADAR_EPAPER ? "DIAL = PICK" : "TAP A DOT", C_DIM, 230, 1);
    }
    const char *b1 = RADAR_EPAPER ? "BACK KEY" : "TAP EMPTY";
    micro_text(cv, GFX_W - 4 - micro_width(b1, 1), GFX_H - 21, b1, C_DIM, 230, 1);
    micro_text(cv, GFX_W - 4 - micro_width("TO GO BACK", 1), GFX_H - 14, "TO GO BACK", C_DIM, 230, 1);
}

#endif

/* --- touch calibration --- */

// 3x3 gauss-jordan with partial pivot, m is [A|b]. returns 0 if singular
static int solve3(float m[3][4], float *x)
{
    for (int c = 0; c < 3; c++) {
        int p = c;
        for (int r = c + 1; r < 3; r++) if (fabsf(m[r][c]) > fabsf(m[p][c])) p = r;
        if (fabsf(m[p][c]) < 1e-6f) return 0;
        if (p != c) for (int j = 0; j < 4; j++) { float t = m[c][j]; m[c][j] = m[p][j]; m[p][j] = t; }
        for (int r = 0; r < 3; r++) {
            if (r == c) continue;
            float f = m[r][c] / m[c][c];
            for (int j = c; j < 4; j++) m[r][j] -= f * m[c][j];
        }
    }
    for (int i = 0; i < 3; i++) x[i] = m[i][3] / m[i][i];
    return 1;
}

void touch_apply(const touch_cal_t *c, int rx, int ry, int *x, int *y)
{
    *x = (int)lroundf(c->a * rx + c->b * ry + c->c);
    *y = (int)lroundf(c->d * rx + c->e * ry + c->f);
}

static void toast(ui_t *u, const char *s)
{
    snprintf(u->toast, sizeof u->toast, "%s", s);
    u->toast_until = u->now + 1800u;
}

void ui_start_calibration(ui_t *u)
{
    u->view = VIEW_CAL;
    u->cal_step = 0;
    u->cal_n = 0;
    u->t_down = 0;
    u->cal_wait = 1;
}

static int median16(const int16_t *v, int n)
{
    int16_t s[48];
    if (n > 48) n = 48;
    for (int i = 0; i < n; i++) {
        int16_t x = v[i];
        int j = i - 1;
        while (j >= 0 && s[j] > x) { s[j + 1] = s[j]; j--; }
        s[j + 1] = x;
    }
    return n ? s[n / 2] : 0;
}

/* 5 crosses, each pressed and held. resistive panel reads wild while pressure
 * rises and falls, so skip first 120 ms, take the median, drop last 2 samples.
 * then least-squares affine fit over all 5, rejected if worst miss > 18 px.
 * NOTE: 3 points with plain means was too fragile, one sloppy press ruined it */
static void cal_touch(ui_t *u, int down, int rx, int ry, uint32_t now, ui_cmd_t *cmd)
{
    if (u->cal_wait) { if (!down) u->cal_wait = 0; return; }
    if (down) {
        if (!u->t_down) { u->t_down = 1; u->t_since = now; u->cal_n = 0; }
        if (now - u->t_since > 120u && u->cal_n < 48) {
            u->cal_sx[u->cal_n] = (int16_t)rx;
            u->cal_sy[u->cal_n] = (int16_t)ry;
            u->cal_n++;
        }
        return;
    }
    if (!u->t_down) return;
    u->t_down = 0;
    /* a press counts after 0.3 s. it used to need 5 samples, but touch is read once a frame,
     * and at 12 fps (3.5" board) that was over half a second: normal presses got ignored */
    if (u->cal_n < 2 || now - u->t_since < 300u) return;    // too short, just a brush
    int n = u->cal_n > 4 ? u->cal_n - 2 : u->cal_n;        // drop the lift-off samples when there are enough
    u->cal_raw[u->cal_step][0] = (float)median16(u->cal_sx, n);
    u->cal_raw[u->cal_step][1] = (float)median16(u->cal_sy, n);
    if (++u->cal_step < CAL_POINTS) return;

    float mx[3][4] = { { 0 } }, my[3][4] = { { 0 } }, X[3], Y[3];
    for (int i = 0; i < CAL_POINTS; i++) {
        float f[3] = { u->cal_raw[i][0], u->cal_raw[i][1], 1.0f };
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) { mx[r][c] += f[r] * f[c]; my[r][c] += f[r] * f[c]; }
            mx[r][3] += f[r] * (float)CAL_PT[i][0];
            my[r][3] += f[r] * (float)CAL_PT[i][1];
        }
    }
    int ok = solve3(mx, X) && solve3(my, Y);
    float worst = 0;
    if (ok) {
        for (int i = 0; i < CAL_POINTS; i++) {
            float ex = X[0] * u->cal_raw[i][0] + X[1] * u->cal_raw[i][1] + X[2] - (float)CAL_PT[i][0];
            float ey = Y[0] * u->cal_raw[i][0] + Y[1] * u->cal_raw[i][1] + Y[2] - (float)CAL_PT[i][1];
            float e = sqrtf(ex * ex + ey * ey);
            if (e > worst) worst = e;
        }
    }
    u->cal_err = worst;
    if (!ok || worst > 18.0f) {
        toast(u, "TRY AGAIN");
        u->cal_step = 0;
        return;
    }
    u->cal = (touch_cal_t){ X[0], X[1], X[2], Y[0], Y[1], Y[2] };
    cmd->save_cal = 1;
    u->view = VIEW_MAIN;
    toast(u, "TOUCH ALIGNED");
}

/* --- touch --- */

// swipe (and MENU key) cycles thru these. round: main already is the full radar.
// paper: the main screen has a radar too, the hunt screen takes the place (skipped without a target)
#if RADAR_ROUND
static const int SWIPE_VIEWS[] = { VIEW_MAIN, VIEW_LIST, VIEW_SPECTRUM, VIEW_MAP };
#elif RADAR_EPAPER
static const int SWIPE_VIEWS[] = { VIEW_MAIN, VIEW_TARGET, VIEW_LIST, VIEW_SPECTRUM, VIEW_MAP };
#else
static const int SWIPE_VIEWS[] = { VIEW_MAIN, VIEW_RADAR, VIEW_LIST, VIEW_SPECTRUM, VIEW_MAP };
#endif

static void select_contact(ui_t *u, int i, ui_cmd_t *cmd)
{
    if (u->snap.sweep == SWEEP_COUNTDOWN || u->snap.sweep == SWEEP_RUN) return;
    cmd->select = 1;
    cmd->select_id = i >= 0 ? u->snap.c[i].id : 0;
}

static void next_theme(ui_t *u, ui_cmd_t *cmd)
{
    ui_set_theme(u, u->theme + 1);
    cmd->save_theme = 1;
    toast(u, TH->name);
}

static void tap_main(ui_t *u, int x, int y, ui_cmd_t *cmd)
{
    const app_t *a = &u->snap;
    if (x < PX - 2) {
        int i = radar_hit(u, &G_MAIN, x, y);
        if (i >= 0) { select_contact(u, i, cmd); return; }
        if (hypotf((float)(x - G_MAIN.cx), (float)(y - G_MAIN.cy)) <= G_MAIN.r_out + 4) u->view = VIEW_RADAR;
        return;
    }
    if (y >= BTN_Y - 2) {
        int b = (x - btn_x(0)) / (BTN_W + 3);
        if (a->sweep != SWEEP_IDLE) { cmd->sweep = a->sweep != SWEEP_RESULT; if (a->sweep == SWEEP_RESULT) select_contact(u, u->target, cmd); return; }
        if (b == 0) cmd->sweep = 1;
        else if (b == 1) u->view = VIEW_MAP;
        else if (u->target >= 0) select_contact(u, -1, cmd);
        else next_theme(u, cmd);
        return;
    }
    if (a->sweep != SWEEP_IDLE) return;
    if (u->target >= 0) { u->view = VIEW_TARGET; return; }
    if (y < 46) { u->view = VIEW_LIST; return; }
    if (y < SPEC_Y - 2) {
        int k = (y - 57) / 18;
        if (y >= 57 && k < (SPEC_Y - 2 - 59) / 18 && k < u->n) select_contact(u, u->order[k], cmd);
        else u->view = VIEW_LIST;
        return;
    }
    u->view = VIEW_SPECTRUM;
}

/* round glass: no buttons. tap a dot to pick it, tap the empty radar to open the
 * target (or the list), the top of the glass is back */
static void tap_round(ui_t *u, int x, int y, ui_cmd_t *cmd)
{
    if (u->view == VIEW_MAIN || u->view == VIEW_RADAR) {
        int i = radar_hit(u, &G_MAIN, x, y);
        if (i >= 0) select_contact(u, i, cmd);
        else if (u->snap.sweep != SWEEP_IDLE) cmd->sweep = u->snap.sweep != SWEEP_RESULT;
        else u->view = u->target >= 0 ? VIEW_TARGET : VIEW_LIST;
        return;
    }
    if (y < LIST_Y0) { u->view = VIEW_MAIN; return; }
    if (u->view == VIEW_LIST) {
        int i = list_row_at(u, y);
        if (i >= 0) { select_contact(u, i, cmd); u->view = VIEW_TARGET; }
    } else if (u->view == VIEW_SPECTRUM) {
        u->chan_sel = spectrum_channel_at(x);
    } else {
        u->view = VIEW_MAIN;
    }
}

static void tap(ui_t *u, int x, int y, ui_cmd_t *cmd)
{
    if (RADAR_ROUND) { tap_round(u, x, y, cmd); return; }
    if (u->view == VIEW_MAIN) { tap_main(u, x, y, cmd); return; }
    if (u->view == VIEW_RADAR) {
        int i = radar_hit(u, &G_FULL, x, y);
        if (i >= 0) select_contact(u, i, cmd);
        else u->view = VIEW_MAIN;
        return;
    }
    if (y < 18) {                                   // header: sort keys on the list, else back
        if (u->view == VIEW_LIST && x >= GFX_W - 120) { u->sort = (x - (GFX_W - 120)) / 40; if (u->sort > 2) u->sort = 2; u->scroll = 0; return; }
        u->view = VIEW_MAIN;
        return;
    }
    switch (u->view) {
    case VIEW_LIST: {
        int i = list_row_at(u, y);
        if (i >= 0) { select_contact(u, i, cmd); u->view = VIEW_TARGET; }
        break;
    }
    case VIEW_SPECTRUM:
        u->chan_sel = spectrum_channel_at(x);
        break;
    case VIEW_TARGET:
        if (y >= GFX_H - 34) {
            int b = x / (GFX_W / 4);
            if (b == 0) { cmd->sweep = 1; u->view = VIEW_MAIN; }
            else if (b == 1) u->view = VIEW_MAP;
            else if (b == 2) {
                int at = 0;
                for (int k = 0; k < u->n; k++) if (u->order[k] == u->target) at = k;
                select_contact(u, u->order[(at + 1) % u->n], cmd);
            } else { select_contact(u, -1, cmd); u->view = VIEW_MAIN; }
        }
        break;
    case VIEW_MAP:
        if (y >= GFX_H - 34) {
            int b = x / ((GFX_W + 1) / 3);
            if (b == 0) { cmd->map_reset = 1; toast(u, "NEW WALK"); }
            else if (b == 1) { cmd->sweep = 1; u->view = VIEW_MAIN; }
            else u->view = VIEW_MAIN;
        }
        break;
    default:
        break;
    }
}

static void long_press(ui_t *u, int x, int y, ui_cmd_t *cmd)
{
    // round has no HUE button: long press on the radar = next colours
    if (RADAR_ROUND && u->view == VIEW_MAIN) { next_theme(u, cmd); return; }
    // long press on HUE = recalibrate touch
    if (u->view == VIEW_MAIN && u->target < 0 && u->snap.sweep == SWEEP_IDLE && x >= btn_x(2) && y >= BTN_Y - 2)
        ui_start_calibration(u);
}

static void swipe(ui_t *u, int dir)
{
    int n = (int)(sizeof SWIPE_VIEWS / sizeof SWIPE_VIEWS[0]), at = 0;
    for (int i = 0; i < n; i++) if (SWIPE_VIEWS[i] == u->view) at = i;
    u->view = SWIPE_VIEWS[(at + dir + n) % n];
    if (u->view == VIEW_TARGET && u->target < 0) u->view = SWIPE_VIEWS[(at + 2 * dir + n) % n];
}

void ui_touch(ui_t *u, int down, int x, int y, int rx, int ry, uint32_t now, ui_cmd_t *cmd)
{
    if (u->view == VIEW_CAL) { cal_touch(u, down, rx, ry, now, cmd); return; }
    if (down && !u->t_down) {
        u->t_down = 1;
        u->t_x0 = u->t_x = x;
        u->t_y0 = u->t_y = y;
        u->t_since = now;
        u->t_moved = 0;
        u->t_long = 0;
        u->t_ns = 0;
        u->t_scroll0 = u->scroll;
    }
    if (down) {
        u->t_x = x;
        u->t_y = y;
        if (u->t_ns < 16) { u->t_sx[u->t_ns] = (int16_t)x; u->t_sy[u->t_ns] = (int16_t)y; u->t_ns++; }
        // first readings of a press wander, so start point = median of first 3
        if (u->t_ns == 3) { u->t_x0 = median16(u->t_sx, 3); u->t_y0 = median16(u->t_sy, 3); }
        if (u->t_ns >= 3 && abs(x - u->t_x0) + abs(y - u->t_y0) > 14) u->t_moved = 1;
        if (u->view == VIEW_LIST && u->t_moved) {
            int maxs = u->snap.n * ROW_H - (LIST_Y1 - LIST_Y0);
            u->scroll = u->t_scroll0 - (y - u->t_y0);
            if (u->scroll > maxs) u->scroll = maxs;
            if (u->scroll < 0) u->scroll = 0;
        }
        if (!u->t_moved && !u->t_long && now - u->t_since >= 1200u) {     // 1.2 s still = long press
            u->t_long = 1;
            long_press(u, u->t_x0, u->t_y0, cmd);
        }
        /* hold still 4 s anywhere = recalibrate. works even when the cal is
         * so off you cant hit any button */
        if (RADAR_TOUCH_CAL && !u->t_moved && u->t_long == 1 && now - u->t_since >= 4000u) {
            u->t_long = 2;
            ui_start_calibration(u);
        }
    } else if (u->t_down) {
        u->t_down = 0;
        int dx = u->t_x - u->t_x0, dy = u->t_y - u->t_y0;
        if (!u->t_moved && !u->t_long) {
            int n = u->t_ns > 2 ? u->t_ns - 1 : u->t_ns;  // drop last sample, its the lift-off
            tap(u, median16(u->t_sx, n), median16(u->t_sy, n), cmd);
        } else if (u->t_moved && abs(dx) > 70 && abs(dx) > 2 * abs(dy)) {   // mostly horizontal = swipe
            swipe(u, dx < 0 ? 1 : -1);
        }
    }
}

// hw button: ev 1 = next network, ev 2 = start 360 turn. on cal screen it skips
void ui_button(ui_t *u, int ev, ui_cmd_t *cmd)
{
    if (u->view == VIEW_CAL) {
        if (ev) { u->view = VIEW_MAIN; toast(u, "TOUCH SKIPPED"); cmd->save_cal = 1; }   // save anyway so we dont ask again
        return;
    }
    if (ev == 2) { cmd->sweep = 1; return; }
    if (ev == 1) {
        int at = -1;
        for (int k = 0; k < u->n; k++) if (u->order[k] == u->target) at = k;
        select_contact(u, at + 1 < u->n ? u->order[at + 1] : -1, cmd);
    }
}

/* pick the network dir steps away from the current one in seq (u->order or u->list).
 * no target yet: down starts at the top, up at the bottom */
static int step_in(const ui_t *u, const uint8_t *seq, int dir)
{
    if (!u->n) return -1;
    int at = -1;
    for (int k = 0; k < u->n; k++) if (seq[k] == u->target) at = k;
    if (at < 0) return seq[dir > 0 ? 0 : u->n - 1];
    at += dir;
    if (at < 0) at = 0;
    if (at >= u->n) at = u->n - 1;
    return seq[at];
}

// keeps the selected row on screen in the list
static void list_follow(ui_t *u, int i)
{
    for (int k = 0; k < u->n; k++) {
        if (u->list[k] != i) continue;
        int top = k * ROW_H, vis = LIST_Y1 - LIST_Y0;
        if (top < u->scroll) u->scroll = top;
        if (top + ROW_H > u->scroll + vis) u->scroll = top + ROW_H - vis;
    }
}

/* boards with keys instead of touch (the e-paper ones have a dial + MENU + BACK).
 * the selected network is the target, so up/down walk thru the networks everywhere
 * except the spectrum, there they walk thru the channels */
void ui_key(ui_t *u, int key, ui_cmd_t *cmd)
{
    const int sweep = u->snap.sweep;
    if (u->view == VIEW_CAL) { ui_button(u, 1, cmd); return; }
    if (key == KEY_OK_LONG) { cmd->sweep = 1; u->view = VIEW_MAIN; return; }
    if (key == KEY_MENU) { swipe(u, 1); return; }
    if (key == KEY_BACK) {
        if (u->view != VIEW_MAIN) { u->view = VIEW_MAIN; return; }
        if (sweep == SWEEP_COUNTDOWN || sweep == SWEEP_RUN) { cmd->sweep = 1; return; }
        // on the main screen BACK walks thru the networks, loudest first, then none:
        // with only the two side keys there is nothing else to pick a target with
        int at = -1;
        for (int k = 0; k < u->n; k++) if (u->order[k] == u->target) at = k;
        select_contact(u, at + 1 < u->n ? u->order[at + 1] : -1, cmd);
        return;
    }
    if (key == KEY_UP || key == KEY_DOWN) {
        int dir = key == KEY_DOWN ? 1 : -1;
        if (u->view == VIEW_SPECTRUM) {
            int ch = u->chan_sel > 0 ? u->chan_sel + dir : (dir > 0 ? 1 : 13);
            u->chan_sel = ch < 1 ? 1 : ch > 13 ? 13 : ch;
        } else if (u->view == VIEW_LIST) {
            int i = step_in(u, u->list, dir);
            if (i >= 0) { select_contact(u, i, cmd); list_follow(u, i); }
        } else if (u->view != VIEW_MAP) {
            int i = step_in(u, u->order, dir);
            if (i >= 0) select_contact(u, i, cmd);
        }
        return;
    }
    if (key != KEY_OK) return;
    if (sweep != SWEEP_IDLE) {                       // OK = abort the turn, or take the result
        cmd->sweep = sweep != SWEEP_RESULT;
        if (sweep == SWEEP_RESULT) select_contact(u, u->target, cmd);
        return;
    }
    switch (u->view) {
    case VIEW_MAIN:
    case VIEW_RADAR:
    case VIEW_LIST:     u->view = u->target >= 0 ? VIEW_TARGET : VIEW_LIST; break;
    case VIEW_SPECTRUM: u->chan_sel = u->chan_sel > 0 ? -1 : u->snap.channel; break;
    case VIEW_MAP:      cmd->map_reset = 1; toast(u, "NEW WALK"); break;
    default:            u->view = VIEW_MAIN; break;
    }
}

/* --- draw one strip of the screen (cv), ui_lap() splits the time --- */

void ui_strip(ui_t *u, canvas_t *cv)
{
    if (ui_clock_us) s_lap = ui_clock_us();
    draw_background(cv);
    ui_lap(u, 0);
    switch (u->view) {
#if RADAR_EPAPER
    // paper has its own screens (ui_paper.c), made for ink, not for light
    case VIEW_TARGET:   if (u->target >= 0) { paper_target(u, cv); break; }   // no target: fall thru to the main one
    // fall through
    case VIEW_RADAR:
    case VIEW_MAIN:     paper_main(u, cv); break;
    case VIEW_LIST:     paper_list(u, cv); break;
    case VIEW_SPECTRUM: paper_spectrum(u, cv); break;
    case VIEW_MAP:      paper_map(u, cv); break;
#else
#if RADAR_ROUND
    case VIEW_LIST:     round_list(u, cv); break;
    case VIEW_TARGET:   if (u->target >= 0) round_target(u, cv); break;
    case VIEW_RADAR:
    case VIEW_MAIN:     round_main(u, cv); break;
#else
    case VIEW_RADAR:    radar_view(u, cv); break;
    case VIEW_LIST:     view_list(u, cv); break;
    case VIEW_TARGET:   if (u->target >= 0) view_target(u, cv); break;
#endif
    case VIEW_SPECTRUM: view_spectrum(u, cv); break;
    case VIEW_MAP:      view_map(u, cv); break;
#endif
    case VIEW_CAL:      view_cal(u, cv); break;
    default:            main_view(u, cv); break;
    }
    ui_lap(u, 3);
    if ((int32_t)(u->toast_until - u->now) > 0) {
        int w = micro_width(u->toast, 2) + 16;
        int ty = u->view == VIEW_CAL ? GFX_H / 2 + 52 : GFX_H / 2 - 16;
        chamfer_box(cv, (GFX_W - w) / 2, ty, w, 26, C_BG0, 220, C_FOCUS, 230);
        micro_text(cv, (GFX_W - w) / 2 + 8, ty + 7, u->toast, C_FOCUS, 255, 2);
    }
    if (u->t_down && u->view != VIEW_CAL) {          // show where touch lands, bad cal is obvious right away
        gfx_ring(cv, (float)u->t_x, (float)u->t_y, 9.0f, 1.2f, C_FOCUS, 210);
        gfx_disc(cv, (float)u->t_x, (float)u->t_y, 1.6f, C_FOCUS, 255);
    }
    ui_lap(u, 4);
}
