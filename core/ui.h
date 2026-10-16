/* ui - the screen and the touch handling.
 * ui_frame lays out once per frame, ui_strip draws strip by strip. ui_touch gets finger state
 * every frame, taps/holds/drags and view switching happen here. anything that changes radio
 * state comes back as ui_cmd_t, the caller applies it under its lock */
#pragma once
#include "app.h"
#include "gfx.h"

enum { VIEW_MAIN, VIEW_RADAR, VIEW_LIST, VIEW_SPECTRUM, VIEW_TARGET, VIEW_MAP, VIEW_CAL };
#define UI_THEMES (RADAR_EPAPER ? 1 : 5)      // e-paper has the one paper theme

typedef struct {
    float x, y;            /* screen pos in the current radar */
    float r;               /* radius, px */
    float ang;             /* screen angle, degrees, 0 = up */
    float sd;              /* +- error of that angle, degrees */
    float bright;          /* phosphor, 0..1 */
    uint32_t col;
    uint8_t ghost;         /* direction unknown, it orbits it's range ring */
    uint8_t src;           /* placed by: 0 nothing, 1 the turn, 2 walk map, 3 both */
    uint8_t sel, dim;
    int     age_ms;
    uint8_t label;
    int16_t lx, ly, lw, lh;
    char    name[40];
    char    sub[16];
} blip_t;

typedef struct {
    uint32_t id;
    float    t;            /* eased radius, 0..1 of the plot */
    float    ang;          /* eased angle */
    uint32_t born;
} anim_t;

// affine map, raw touch chip numbers -> screen pixels
typedef struct { float a, b, c, d, e, f; } touch_cal_t;

// what the caller has to do with radio state (under its lock)
typedef struct {
    uint8_t  select;       /* make select_id the target (0 = none) */
    uint32_t select_id;
    uint8_t  sweep;        /* start or abort the 360 turn */
    uint8_t  map_reset;
    uint8_t  save_cal;     /* new calibration in u->cal, save it */
    uint8_t  save_theme;
} ui_cmd_t;

typedef struct {
    uint32_t now, boot;
    app_t    snap;          /* private copy of radio state for this frame */
    int      view, theme;
    float    beam;
    blip_t   b[MAX_CONTACTS];
    anim_t   anim[MAX_CONTACTS];
    uint8_t  order[MAX_CONTACTS];
    int      n, target;
    float    spectrum[14];
    const char *up_note;    /* what "up" means right now */
    /* list view */
    int      scroll;        /* pixels */
    int      sort;          /* 0 signal, 1 name, 2 channel */
    uint8_t  list[MAX_CONTACTS];
    /* spectrum view */
    int      chan_sel;
    /* touch */
    int      t_down, t_x0, t_y0, t_x, t_y, t_moved, t_long, t_scroll0;
    uint32_t t_since;
    /* calibration */
    touch_cal_t cal;
    int      cal_step;
    float    cal_raw[5][2];
    int16_t  cal_sx[48], cal_sy[48];
    int      cal_n;
    float    cal_err;           /* worst cross after the fit, px */
    int      cal_wait;          /* wait untill the finger that asked for alignment is lifted */
    /* samples of one press, the tap goes to the median */
    int16_t  t_sx[16], t_sy[16];
    int      t_ns;
    uint32_t toast_until;
    char     toast[32];
    uint32_t prof[8];       /* us per layer, summed (only if ui_clock_us is set) */
} ui_t;

/* profiling: point this at a microsecond clock and ui_strip times its layers into prof[]
 * 0 background, 1 radar disc, 2 decor, 3 panel or view, 4 toast, 5 radar rest */
extern uint32_t (*ui_clock_us)(void);

/* the firmware sets this on boards sold with several screens (the 2.8" CYD revisions): the name of
 * the screen start-up in use. the touch alignment page then says BOOT picks another. NULL = one screen */
extern const char *ui_screen_name;

int  ui_init(ui_t *u, uint32_t now);    // builds lookup tables, 0 = out of memory
// copy radio state into u->snap before this
void ui_frame(ui_t *u, uint32_t now);
void ui_strip(ui_t *u, canvas_t *cv);

/* call every frame: down, x/y calibrated, rx/ry raw from the chip (for calibration).
 * fills *cmd */
void ui_touch(ui_t *u, int down, int x, int y, int rx, int ry, uint32_t now, ui_cmd_t *cmd);
// BOOT button, ev 1 = short press, 2 = long
void ui_button(ui_t *u, int ev, ui_cmd_t *cmd);
// boards with a dial and keys (the e-paper ones): one event per press
enum { KEY_UP = 1, KEY_DOWN, KEY_OK, KEY_OK_LONG, KEY_MENU, KEY_BACK };
void ui_key(ui_t *u, int key, ui_cmd_t *cmd);
void ui_start_calibration(ui_t *u);
void ui_set_theme(ui_t *u, int theme);
// raw touch -> screen pixels
void touch_apply(const touch_cal_t *c, int rx, int ry, int *x, int *y);
