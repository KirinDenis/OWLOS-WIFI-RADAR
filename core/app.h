/*
 * app - what the radio heard and what we make of it.
 * plain C, no ESP-IDF in here: firmware feeds real frames, PC preview feeds a simulated flat.
 */
/* direction: rssi says how loud, never from where. But a body eats 2.4 GHz quite well
 * (5-15 dB), so hold it at the chest and turn once on the spot with the beam - each AP
 * is loudest when you face it. No compass, heading is just TIME into the turn, so turn
 * steady. Indoors its where the signal ARRIVES from (walls, open doors bend it). */
#pragma once
#include <stdint.h>

#define MAX_CONTACTS 40
#define HIST_LEN     60          // 1 value per second = last minute
#define HIST_NONE    (-128)
#define BEAR_BINS    24          // 15 deg each

#define MAP_N        16    // max networks in the walk map
#define MAP_T        160   // walk snapshots, when full pairs get merged

enum { SEC_OPEN, SEC_WEP, SEC_WPA, SEC_WPA2, SEC_WPA3 };
enum { SMP_BEACON = 1, SMP_DATA = 2 };
enum { SWEEP_IDLE, SWEEP_COUNTDOWN, SWEEP_RUN, SWEEP_RESULT };

#define SWEEP_COUNTDOWN_MS 3000u
#define SWEEP_RUN_MS       24000u   // one full turn, 15 deg/s
#define SWEEP_RESULT_MS    3500u

// one received frame, only the fields we keep
typedef struct {
    uint8_t bssid[6];
    int8_t  rssi;
    uint8_t ch_rx;      // channel we were tuned to
    uint8_t ch_ap;      // AP's own channel from beacon, 0 = unknown
    uint8_t kind;       // SMP_BEACON (beacon or probe resp) or SMP_DATA
    uint8_t sec;
    uint8_t ssid_len;
    char    ssid[32];
} sample_t;

typedef struct {
    uint32_t id;               // stable while contact lives, never 0
    uint8_t  bssid[6];
    char     ssid[33];         // utf-8, "" if hidden
    uint8_t  channel, sec, hidden;
    float    rssi;             // dBm, ~1.5 s smoothing
    float    rssi_slow;        // ~12 s smoothing
    int8_t   rssi_last, rssi_peak;
    uint32_t first_ms, last_ms, n;
    int8_t   hist[HIST_LEN];   // ring buffer, hist_head = next write
    uint8_t  hist_head;
    float    sec_acc;          // sum/count for the current second
    uint8_t  sec_n;
    // bearing from the last turn
    uint8_t  has_bearing;
    float    bearing;          // deg clockwise, 0 = where you faced at the start
    float    bearing_sd;       // +- deg
    float    swing_db;         // loudest minus quietest over the turn
    float    bin_sum[BEAR_BINS];
    uint8_t  bin_n[BEAR_BINS];
} contact_t;

/* walk map as the screen gets it, the solving is in map.c.
 * metres, x right, y up; up = way you first walked (or faced, after a turn fixed it) */
typedef struct {
    uint8_t  active;            // solution good enough to draw
    uint8_t  aligned;           // left/right fixed by a 360 turn
    uint8_t  heading_ok;
    uint8_t  n;
    uint32_t id[MAP_N];
    float    ax[MAP_N], ay[MAP_N], sd[MAP_N];
    uint8_t  ok[MAP_N];         // placed good enough to trust the direction to it
    int16_t  T;
    float    px[MAP_T], py[MAP_T];
    float    heading;           // compass deg, map frame
    float    spread;            // m, how far the walk got from its middle
    float    since_turn;        // m away from the spot of the last 360 turn
    float    motion;            // 0..1, how much of the level changes the walk explains
    uint8_t  turned;            // did a turn during this walk
    uint32_t seconds;
} mapview_t;

typedef struct {
    contact_t c[MAX_CONTACTS];
    int       n;
    uint32_t  next_id;
    uint32_t  hist_sec;
    // radio
    uint8_t   channel;         // current channel
    uint32_t  pkts;            // frames used, total
    uint16_t  pkt_rate;        // frames/s
    uint32_t  rate_pkts, rate_t0;
    uint8_t   hop_idx, hop_sub;
    // 360 turn
    uint8_t   sweep;
    uint32_t  sweep_t0;        // when current phase started
    uint8_t   sweep_resolved, sweep_total;
    uint32_t  bearing_ms;      // last time bearings were solved, 0 = never
    // selection
    uint32_t  target_id;       // 0 = none
    mapview_t map;
} app_t;

void  app_init(app_t *a, uint32_t now);
void  app_sample(app_t *a, const sample_t *s, uint32_t now);
void  app_tick(app_t *a, uint32_t now);
// which channel next and how long to stay. only decides, the caller does the tuning
int   app_next_channel(app_t *a, uint32_t *dwell_ms);

void  app_select_next(app_t *a);
void  app_select(app_t *a, uint32_t id);             // 0 = none
void  app_sweep_toggle(app_t *a, uint32_t now);
float app_heading(const app_t *a, uint32_t now);      // degrees into the turn

const contact_t *app_find(const app_t *a, uint32_t id);
float app_distance_m(float rssi);                     // very rough, log-distance model
float app_trend(const contact_t *c);                  // dB per 10 s, over last 10 s
int   app_sorted(const app_t *a, uint8_t *idx);       // indices, loudest first
