/*
 * walk - tests the walk map on a fake 10 x 7 m flat where we know the answer: inner walls
 * with doors, own router + printer, neighbours, one router a floor up. 2 laps through
 * every room with the radar at the chest (body shadows whats behind), then a 360 turn.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "preview.h"
#include "../../core/map.h"
#include "../../core/store.h"

typedef struct { const char *ssid; uint8_t ch, sec; float x, y, z, A, busy; } wap_t;

static const wap_t W[] = {
    { "Kirin82",              6, SEC_WPA2,  1.0f,  1.0f, 0.0f, -36, 3 },   /* living room corner */
    { "Kirin82-Guest",        6, SEC_WPA2,  1.0f,  1.2f, 0.0f, -36, 0 },
    { "HP-Print-3B-LaserJet", 6, SEC_WPA,   9.2f,  6.2f, 0.0f, -40, 0 },   /* bedroom */
    { "Byt_Sokolova_12",      3, SEC_WPA2, -5.0f,  3.0f, 0.0f, -37, 0 },   /* west neighbour */
    { "Telekom-8F2A",        11, SEC_WPA3, 15.0f,  2.0f, 0.0f, -35, 1 },   /* east neighbour */
    { "O2-Internet-427",      1, SEC_WPA2,  4.0f, -7.0f, 0.0f, -38, 0 },   /* across the street */
    { "Domáca sieť Žilina",   6, SEC_WPA2,  6.0f, 12.0f, 0.0f, -39, 0 },   /* north neighbour */
    { "Квартира 7",          11, SEC_WPA2,  3.0f,  3.0f, 3.0f, -37, 0 },   /* a floor up */
    { "TP-Link_5C1E",        11, SEC_WPA2, 12.0f,  9.0f, 0.0f, -40, 0 },
    { "UPC Wi-Free",          1, SEC_OPEN, -3.0f, -6.0f, 0.0f, -36, 0 },
    { "",                     9, SEC_WPA2,  8.0f, -4.0f, 0.0f, -38, 0 },   /* hidden */
};
#define NW ((int)(sizeof W / sizeof W[0]))

typedef struct { float x0, y0, x1, y1; } seg_t;
static const seg_t INNER[] = {
    { 4.5f, 0.0f, 4.5f, 2.5f }, { 4.5f, 3.5f, 4.5f, 7.0f },      /* door at y 2.5..3.5 */
    { 4.5f, 3.5f, 7.0f, 3.5f }, { 8.0f, 3.5f, 10.0f, 3.5f },     /* door at x 7..8 */
};
static const seg_t OUTER[] = { { 0, 0, 10, 0 }, { 10, 0, 10, 7 }, { 10, 7, 0, 7 }, { 0, 7, 0, 0 } };

static const float PATH[][2] = {
    { 2.0f, 2.0f }, { 2.0f, 5.5f }, { 3.8f, 5.5f }, { 3.8f, 3.0f }, { 6.0f, 3.0f }, { 9.0f, 1.2f },
    { 6.0f, 1.2f }, { 7.5f, 3.0f }, { 7.5f, 4.5f }, { 9.0f, 6.0f }, { 5.5f, 6.0f }, { 7.5f, 4.5f },
    { 7.5f, 3.0f }, { 5.0f, 3.0f }, { 2.0f, 2.0f },
};
#define NP ((int)(sizeof PATH / sizeof PATH[0]))
#define SPEED 0.8f
#define LAPS 2
static int g_still;   // 1 = radar lies on a table the whole time, nobody walks

static float compass(float dx, float dy)
{
    float a = atan2f(dx, dy) * 57.29578f;
    return a < 0 ? a + 360.0f : a;
}

static int crosses(seg_t s, float ax, float ay, float bx, float by)
{
    float d1x = bx - ax, d1y = by - ay, d2x = s.x1 - s.x0, d2y = s.y1 - s.y0;
    float den = d1x * d2y - d1y * d2x;
    if (fabsf(den) < 1e-6f) return 0;
    float u = ((s.x0 - ax) * d2y - (s.y0 - ay) * d2x) / den;
    float v = ((s.x0 - ax) * d1y - (s.y0 - ay) * d1x) / den;
    return u > 0 && u < 1 && v > 0 && v < 1;
}

static float lap_length(void)
{
    float L = 0;
    for (int i = 1; i < NP; i++) L += hypotf(PATH[i][0] - PATH[i - 1][0], PATH[i][1] - PATH[i - 1][1]);
    return L;
}

// where the person is at time s and which way they face
static void where(float s, float *x, float *y, float *face)
{
    if (g_still) { *x = 2.0f; *y = 2.0f; *face = 0.0f; return; }
    float L = lap_length(), d = s * SPEED;
    if (d > L * LAPS) d = L * LAPS - 0.01f;
    d = fmodf(d, L);
    for (int i = 1; i < NP; i++) {
        float l = hypotf(PATH[i][0] - PATH[i - 1][0], PATH[i][1] - PATH[i - 1][1]);
        if (d <= l) {
            float f = d / l;
            *x = PATH[i - 1][0] + (PATH[i][0] - PATH[i - 1][0]) * f;
            *y = PATH[i - 1][1] + (PATH[i][1] - PATH[i - 1][1]) * f;
            *face = compass(PATH[i][0] - PATH[i - 1][0], PATH[i][1] - PATH[i - 1][1]);
            return;
        }
        d -= l;
    }
    *x = PATH[NP - 1][0]; *y = PATH[NP - 1][1]; *face = 0;
}

/* levels differ from the solver's model on purpose: path loss exponent 3.0 (model has 2.8),
 * every router own loudness, walls 4 dB inside / 6 dB outside, shadowing, body, 3 dB fading */
static int8_t level(int i, float x, float y, float face, int off)
{
    const wap_t *w = &W[i];
    float dx = w->x - x, dy = w->y - y;
    float d = sqrtf(dx * dx + dy * dy + w->z * w->z);
    float p = w->A - 30.0f * log10f(d < 0.5f ? 0.5f : d);
    for (int k = 0; k < 4; k++) p -= 4.0f * (float)crosses(INNER[k], x, y, w->x, w->y);
    for (int k = 0; k < 4; k++) p -= 6.0f * (float)crosses(OUTER[k], x, y, w->x, w->y);
    if (w->z > 1.0f) p -= 8.0f;                                    // floor slab
    float rel = (compass(dx, dy) - face) * 0.0174533f;
    p -= 8.0f * (1.0f - cosf(rel)) * 0.5f;                         // own body
    p += 2.5f * sinf(0.9f * x + (float)i) * cosf(0.7f * y + 2.0f * (float)i);   // shadowing, smooth over the flat
    p += 3.0f * gauss();                                           // fading
    p -= 14.0f * (float)off;
    if (p > -20.0f) p = -20.0f;
    return (int8_t)lroundf(p);
}

static void bssid(int i, uint8_t *m)
{
    m[0] = 0x02; m[1] = 0x57; m[2] = 0xA1;
    m[3] = (uint8_t)(i * 17); m[4] = (uint8_t)(i * 31 + 5); m[5] = (uint8_t)(i * 7 + 9);
}

/* best rigid fit of est onto truth: rotate, mirror, shift. never scale, getting
 * the scale right is the model's job. returns rms */
static float rigid(const float *ex, const float *ey, const float *tx, const float *ty, int n,
                   float *c, float *s, int *mir, float *ox, float *oy, float *mx, float *my)
{
    float msx = 0, msy = 0, mtx = 0, mty = 0;
    for (int i = 0; i < n; i++) { msx += ex[i]; msy += ey[i]; mtx += tx[i]; mty += ty[i]; }
    msx /= n; msy /= n; mtx /= n; mty /= n;
    float Sxx = 0, Sxy = 0, Syx = 0, Syy = 0;
    for (int i = 0; i < n; i++) {
        float sx = ex[i] - msx, sy = ey[i] - msy, qx = tx[i] - mtx, qy = ty[i] - mty;
        Sxx += sx * qx; Sxy += sx * qy; Syx += sy * qx; Syy += sy * qy;
    }
    float a1 = Sxx + Syy, b1 = Sxy - Syx, a2 = Syy - Sxx, b2 = -Sxy - Syx;
    *mir = a2 * a2 + b2 * b2 > a1 * a1 + b1 * b1;
    float th = *mir ? atan2f(b2, a2) : atan2f(b1, a1);
    *c = cosf(th); *s = sinf(th); *mx = msx; *my = msy; *ox = mtx; *oy = mty;
    float e = 0;
    for (int i = 0; i < n; i++) {
        float x = ex[i] - msx, y = ey[i] - msy;
        if (*mir) x = -x;
        float rx = *c * x - *s * y + mtx, ry = *s * x + *c * y + mty;
        e += (rx - tx[i]) * (rx - tx[i]) + (ry - ty[i]) * (ry - ty[i]);
    }
    return sqrtf(e / n);
}

int walk_main(const char *dir, int still)
{
    g_still = still;
    static app_t app;
    static map_t map;
    static ui_t ui;
    static float tx[MAP_T], ty[MAP_T];
    app_init(&app, 0);
    map_reset(&map);
    ui_init(&ui, 0);

    float walk_s = lap_length() * LAPS / SPEED;
    uint32_t walk_ms = (uint32_t)(walk_s * 1000.0f);
    uint32_t turn_at = walk_ms + 3000, end_at = turn_at + SWEEP_COUNTDOWN_MS + SWEEP_RUN_MS + 2500;
    if (still) printf("\nstill: the radar lies on a table for %.0f s, then a 360 turn\n", walk_s);
    else printf("\nwalk: %.0f m in %.0f s, then a 360 turn\n", lap_length() * LAPS, walk_s);

    uint32_t t = 0, next_sec = 1000, last_brg = 0, dwell;
    float stop_face = 0;
    int shot_walk = 0, turned = 0;
    while (t < end_at) {
        int ch = app_next_channel(&app, &dwell);
        app.channel = (uint8_t)ch;
        for (uint32_t e = t + dwell; t < e; t += 10) {
            float x, y, face;
            where((float)t / 1000.0f, &x, &y, &face);
            if (t >= walk_ms) {
                if (!stop_face) stop_face = face + 0.001f;
                face = stop_face;
                if (app.sweep == SWEEP_RUN) face += app_heading(&app, t);
            }
            for (int i = 0; i < NW; i++) {
                int off = abs(W[i].ch - ch);
                if (off > 2 || ((t + (uint32_t)i * 37u) % 102u) >= 10u) continue;
                sample_t s;
                memset(&s, 0, sizeof s);
                bssid(i, s.bssid);
                s.rssi = level(i, x, y, face, off);
                if (s.rssi < -94) continue;
                if (frand() > (s.rssi + 95.0f) / 25.0f) continue;   // weak beacons get lost
                s.ch_rx = (uint8_t)ch;
                s.ch_ap = W[i].ch;
                s.kind = SMP_BEACON;
                s.sec = W[i].sec;
                s.ssid_len = (uint8_t)strlen(W[i].ssid);
                memcpy(s.ssid, W[i].ssid, s.ssid_len);
                app_sample(&app, &s, t);
                for (int k = 0; k < (int)W[i].busy; k++) {
                    s.kind = SMP_DATA;
                    s.rssi = level(i, x, y, face, off);
                    app_sample(&app, &s, t + 1);
                }
            }
            app_tick(&app, t);
            if (t >= next_sec) {
                next_sec += 1000;
                int before = map.T;
                map_snapshot(&map, &app);
                if (map.T > before) {
                    float mx, my, mf;
                    where((float)t / 1000.0f - 0.5f, &mx, &my, &mf);
                    tx[map.T - 1] = mx; ty[map.T - 1] = my;
                }
                map_solve(&map, 4);
                if (app.bearing_ms != last_brg) {
                    last_brg = app.bearing_ms;
                    float R = map_align_turn(&map, &app);
                    printf("  turn: %d/%d bearings, map agreement %.2f\n", app.sweep_resolved, app.sweep_total, R);
                }
                map_publish(&map, &app);
                if (!still && t >= 12000 && shot_walk == 0) { shot_walk = 1; ui.view = VIEW_MAP; render(&ui, &app, t); shot(dir, "20-walk-start", 2); ui.view = VIEW_MAIN; }
                if (!still && t >= 70000 && shot_walk == 1) { shot_walk = 2; ui.view = VIEW_MAP; render(&ui, &app, t); shot(dir, "21-walk-map", 2); ui.view = VIEW_MAIN; render(&ui, &app, t); shot(dir, "22-walk-radar", 2); }
            }
            if (!turned && t >= turn_at) { turned = 1; app_sweep_toggle(&app, t); }
            if (t % 40 == 0) { ui.snap = app; ui_frame(&ui, t); }
        }
    }
    if (!still) {
        render(&ui, &app, t);
        shot(dir, "24-walk-radar-turned", 2);
        ui.view = VIEW_MAP;
        render(&ui, &app, t);
        shot(dir, "23-walk-map-turned", 2);
    }

    // so how good is it? path vs truth, then every router
    const mapview_t *v = &app.map;
    float c, s, ox, oy, mx, my;
    int mir;
    float rms = rigid(v->px, v->py, tx, ty, v->T, &c, &s, &mir, &ox, &oy, &mx, &my);
    printf("walk solved: %d snapshots, path rms %.2f m after the best rigid fit%s, spread %.1f m, %s\n",
           v->T, rms, mir ? " (mirrored)" : "", v->spread, v->aligned ? "aligned by the turn" : "NOT aligned");
    printf("  motion explained %.2f, map %s\n", v->motion, v->active ? "ACTIVE" : "off");
    if (still) return 0;

    // store round trip: save to a file, read back, must be identical
    {
        static map_t back;
        static saved_net_t nets[MAX_CONTACTS], nb[MAX_CONTACTS];
        char path[512];
        snprintf(path, sizeof path, "%s/STATE.BIN", dir);
        int n = store_collect(&app, nets), nn = 0, ok = 0;
        FILE *f = fopen(path, "wb");
        if (f) { ok = store_save(f, &map, nets, n); fclose(f); }
        f = fopen(path, "rb");
        if (f && ok) { ok = store_load(f, &back, nb, &nn); fclose(f); }
        long size = 0;
        f = fopen(path, "rb");
        if (f) { fseek(f, 0, SEEK_END); size = ftell(f); fclose(f); }
        int same = ok && back.T == map.T && back.n == map.n && nn == n &&
                   !memcmp(back.ax, map.ax, sizeof map.ax) && !memcmp(back.m, map.m, sizeof map.m);
        printf("  store: %ld bytes, read back %s (T %d, %d networks)\n", size, same ? "identical" : "DIFFERENT", back.T, nn);
        remove(path);
    }
    float px_now, py_now, f_now;
    where(walk_s, &px_now, &py_now, &f_now);
    double be = 0; int nb = 0;
    for (int k = 0; k < v->n; k++) {
        int wi = -1;
        for (int j = 0; j < app.n; j++) {
            if (app.c[j].id != v->id[k]) continue;
            for (int i = 0; i < NW; i++) { uint8_t m[6]; bssid(i, m); if (!memcmp(m, app.c[j].bssid, 6)) wi = i; }
        }
        if (wi < 0) continue;
        float x = v->ax[k] - mx, y = v->ay[k] - my;
        if (mir) x = -x;
        float rx = c * x - s * y + ox, ry = s * x + c * y + oy;
        float err = hypotf(rx - W[wi].x, ry - W[wi].y);
        // radar bearing to it right now vs the truth
        float est = compass(v->ax[k] - v->px[v->T - 1], v->ay[k] - v->py[v->T - 1]) - v->heading;
        float tru = compass(W[wi].x - px_now, W[wi].y - py_now) - stop_face;
        float berr = fmodf(est - tru + 720.0f + 180.0f, 360.0f) - 180.0f;
        float turn = -1;
        for (int j = 0; j < app.n; j++) if (app.c[j].id == v->id[k] && app.c[j].has_bearing) turn = app.c[j].bearing;
        printf("  %-22s pos err %5.1f m  sd %5.1f  %s  bearing err %+6.1f   (map %5.1f turn %5.1f true %5.1f hdg %5.1f)\n",
               W[wi].ssid[0] ? W[wi].ssid : "[hidden]", err, v->sd[k], v->ok[k] ? "ok " : "-- ", berr,
               fmodf(est + 720.0f, 360.0f), turn, fmodf(tru + 720.0f, 360.0f), v->heading);
        if (v->ok[k]) { be += berr * berr; nb++; }
    }
    if (nb) printf("  bearing rms %.1f deg over %d placed networks\n", sqrt(be / nb), nb);
    return 0;
}
