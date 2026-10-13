/*
 * map - where the networks are, learned from a walk. basically the simplest WiFi SLAM.
 */
/* standing still rssi gives only a distance. walking, the levels go up and down with
 * where you are, so once a second we keep a snapshot of all levels and solve the walk
 * and the APs (position + level at 1 m) together, so rssi = A - 10 n log10(d) fits best.
 * smoothness prior (people move ~1 m/s) is what makes it solvable: path step is one
 * block-tridiagonal solve, APs get trilaterated against it, repeat. */
/* a walk cant tell a mirrored or rotated map, they fit the same. rotation is pinned to
 * the way you first walked, left/right stays unknown until a 360 turn (body shadow,
 * app.h) is matched to the map. */
#pragma once
#include <stdint.h>
#include "app.h"

typedef struct {
    uint32_t id[MAP_N];
    uint8_t  bssid[MAP_N][6];  // the real key, ids change every power-on
    int      n;
    int8_t   m[MAP_T][MAP_N];   // dBm, HIST_NONE = not heard that second
    float    dt[MAP_T];         // s since previous snapshot
    int      T;
    uint32_t seconds;
    float    pending;           // s since the last kept snapshot
    // solution, in the solver's own frame
    float    px[MAP_T], py[MAP_T];
    float    ax[MAP_N], ay[MAP_N], A[MAP_N], sd[MAP_N];
    int      have;
    float    cost;
    uint32_t rng;
    int      next_restart;
    // published frame: solution rotated/mirrored/moved onto the previous one,
    // so the picture does not jump when the solver flips or turns the whole thing
    float    qx[MAP_T], qy[MAP_T], bx[MAP_N], by[MAP_N];
    int      published;
    int      framed;            // up = first walking direction
    int      aligned;           // left/right and up fixed by a turn
    float    heading;
    int      heading_ok;
    int      turn_t;            // snapshot index of the last turn
    int      turned;
} map_t;

void map_reset(map_t *m);
// NOTE: once a second, AFTER app_tick rolled the history
void map_snapshot(map_t *m, const app_t *a);
// about `iters` rounds of path step + AP steps
void map_solve(map_t *m, int iters);

/* called after every round of the solver. the restarts are ~180 rounds: 1.2-1.7 s of
 * maths on the ESP32 (measured, 160 snapshots, 16 networks), but next to a busy router the
 * wifi took most of core 0 and it stretched to 10 s. nothing of lower priority ran
 * meanwhile, the idle task starved and the task watchdog rebooted the board.
 * the firmware points this at vTaskDelay(1). NULL = no-op */
extern void (*map_yield)(void);

/* how many fresh starts compete, and how often (s). 6 every 20 s by default. a chip
 * without an FPU (ESP32-C3) needs 6-10 s for them even with 4 networks, so the firmware
 * makes it fewer and rarer there */
extern int map_restarts, map_restart_every;
// after a 360 turn: match its bearings to the map, fixes left/right and up
float map_align_turn(map_t *m, const app_t *a);   // agreement 0..1, negative = couldnt try
// copy solution (published frame) into a->map for the screen
void map_publish(map_t *m, app_t *a);
