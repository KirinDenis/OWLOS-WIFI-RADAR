// ui internals, shared between ui.c, ui_radar.c and ui_views.c
#pragma once
#include "ui.h"
#include "font.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- themes. every colour has one meaning, same in all 5 themes --- */
typedef struct {
    const char *name;
    uint32_t bg0, bg1, glass, panel;
    uint32_t prim;    /* the instrument + protected networks */
    uint32_t quiet;   /* instrument, quieter */
    uint32_t focus;   /* beam edge, the target, instructions */
    uint32_t dim;     /* secondary text */
    uint32_t open;    /* open network */
    uint32_t weak;    /* broken security: WEP, WPA1 */
} theme_t;

extern const theme_t UI_THEME[UI_THEMES];
extern const theme_t *TH;

#define C_BG0    (TH->bg0)
#define C_BG1    (TH->bg1)
#define C_GLASS  (TH->glass)
#define C_PANEL  (TH->panel)
#define C_PRIM   (TH->prim)
#define C_QUIET  (TH->quiet)
#define C_FOCUS  (TH->focus)
#define C_DIM    (TH->dim)
#define C_OPEN   (TH->open)
#define C_WEAK   (TH->weak)

// polar tables cover one quadrant of the biggest radar (the full screen one, r_out = H/2 - 5)
#define RQ_MAX   (GFX_H / 2 + 4)

/* --- radar geometry, main screen and full screen --- */
typedef struct {
    int cx, cy, r_out, r_lbl, r_plot;
    uint8_t ring[RQ_MAX * 16];    /* instrument light (additive), index = radius*16 */
    uint8_t dash[RQ_MAX * 16];    /* dBm rings, get dashed by angle later */
    uint8_t glass[RQ_MAX];        /* glass alpha by radius */
} rgeo_t;

extern rgeo_t G_MAIN, G_FULL;

#define RSSI_NEAR (-28.0f)
#define RSSI_FAR  (-92.0f)
#define TREND_DB  3.0f            /* walking changes level this much in 10 s, fading rarely does */
#define LABEL_W   66
#define D2R       (3.14159265f / 180.0f)

// right panel on the main screen. worked out from the screen size in radar_init()
// (for 320x240: PX 228, PW 91)
extern int UI_PX, UI_PW;
#define PX   UI_PX
#define PW   UI_PW
#define TX   (PX + 6)
#define TX2  (PX + PW - 5)

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float wrap360(float a) { a = fmodf(a, 360.0f); return a < 0 ? a + 360.0f : a; }
static inline float wrap180(float a) { a = wrap360(a); return a > 180.0f ? a - 360.0f : a; }

static inline float rssi_frac(float rssi)
{
    return clampf((RSSI_NEAR - rssi) / (RSSI_NEAR - RSSI_FAR), 0.06f, 1.0f);
}

uint32_t    sec_color(int sec);
const char *sec_name(int sec);
const char *net_name(const contact_t *c);
float       compass_deg(float dx, float dy);

void arrow(canvas_t *cv, float x, float y, int up, uint32_t c);
void button(canvas_t *cv, int x, int y, int w, int h, const char *label, int lit);
void chamfer_box(canvas_t *cv, int x, int y, int w, int h, uint32_t fill, int fill_a, uint32_t line, int line_a);
void view_header(canvas_t *cv, const char *title, const char *right);
void key_hints(canvas_t *cv, int x, int y, int w, int h, const char *l1, const char *l2);   /* e-paper: what the keys do */
void draw_background(canvas_t *cv);
void ui_lap(ui_t *u, int layer);
void draw_spectrum_small(ui_t *u, canvas_t *cv, int y, int h);
void draw_sparkline(canvas_t *cv, const contact_t *c, int x, int y, int w, int h, int big);

/* radar */
int  radar_init(void);
void radar_place(ui_t *u, const rgeo_t *g);        /* blip positions + labels for g */
void radar_draw(ui_t *u, canvas_t *cv, const rgeo_t *g, int full);
int  radar_hit(const ui_t *u, const rgeo_t *g, int x, int y);   /* contact index or -1 */

/* full-screen views */
void view_list(ui_t *u, canvas_t *cv);
void view_spectrum(ui_t *u, canvas_t *cv);
void view_target(ui_t *u, canvas_t *cv);
void view_map(ui_t *u, canvas_t *cv);
void view_cal(ui_t *u, canvas_t *cv);
int  list_row_at(const ui_t *u, int y);             /* contact index or -1 */
void list_order(ui_t *u);
int  spectrum_channel_at(int x);
int  spectrum_channel_x(int ch);                    /* centre of a channel on the big spectrum */

/* list rows. round glass: start lower and stop higher, the corners are not there */
#define ROW_H    22
#define LIST_Y0  (RADAR_ROUND ? 40 : 20)
#define LIST_Y1  (RADAR_ROUND ? GFX_H - 34 : GFX_H)

/* round screens get their own main, target and list views (ui_round.c) */
void round_main(ui_t *u, canvas_t *cv);
void round_target(ui_t *u, canvas_t *cv);
void round_list(ui_t *u, canvas_t *cv);

/* e-paper gets its own main screen, a dashboard in ViewOwl's style (ui_paper.c).
 * the grey header band is PAPER_HEAD tall, the radar sits under it on the left */
#define PAPER_HEAD 44
void paper_main(ui_t *u, canvas_t *cv);
void paper_target(ui_t *u, canvas_t *cv);
void paper_list(ui_t *u, canvas_t *cv);
void paper_spectrum(ui_t *u, canvas_t *cv);
void paper_map(ui_t *u, canvas_t *cv);

// touch calibration targets: 4 corners 30 px in + centre (set in ui_init, round screens
// pull the corners in so they stay on the glass)
#define CAL_POINTS 5
extern int CAL_PT[CAL_POINTS][2];
