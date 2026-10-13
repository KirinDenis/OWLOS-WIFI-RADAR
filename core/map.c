#include "map.h"
#include <math.h>
#include <string.h>

#define KDB      12.16f     // 10 n / ln 10 with n = 2.8, dB per ln(metre)
#define D0SQ     0.5f       // (0.7 m)^2, nobody stands inside the router
#define A0       (-38.0f)   // typical router level at 1 m
#define LAM_A    0.25f      // how hard A is pulled to A0: (4 dB / 8 dB)^2
#define SIG2     16.0f      // (4 dB)^2, noise of one second's level
#define VWALK2   1.0f       // (1 m/s)^2, walking speed
#define HUBER    6.0f       // past 6 dB a miss counts linear (doors, fridges...)
#define RESTARTS 6
#define MOTION_MIN 0.35f   // see motion_evidence, picked from walk vs still simulations
#define PI_F     3.14159265f

// cheap LCG, returns 0..1
static float frnd(map_t *m)
{
    m->rng = m->rng * 1664525u + 1013904223u;
    return (float)(m->rng >> 8) / 16777216.0f;
}

static float compass(float dx, float dy)            // degrees, 0 = up, clockwise
{
    float a = atan2f(dx, dy) * (180.0f / PI_F);
    return a < 0 ? a + 360.0f : a;
}

// rotate point around origin so its compass angle grows by deg
static void rot_cw(float *x, float *y, float deg)
{
    float r = deg * (PI_F / 180.0f), c = cosf(r), s = sinf(r);
    float nx = *x * c + *y * s, ny = -*x * s + *y * c;
    *x = nx;
    *y = ny;
}

void map_reset(map_t *m)
{
    uint32_t r = m->rng ? m->rng : 0x2545F491u;
    memset(m, 0, sizeof *m);
    m->rng = r;
}

// slots are keyed by BSSID, not radio ids: ids are new after each power-on
// and a saved map has to find its networks again
static int slot_of(const map_t *m, const uint8_t *bssid)
{
    for (int k = 0; k < m->n; k++) if (memcmp(m->bssid[k], bssid, 6) == 0) return k;
    return -1;
}

// buffer full: merge every 2 neighbour snapshots into 1. walk keeps its shape,
// only time resolution halves
static void decimate(map_t *m)
{
    int h = m->T / 2;
    for (int j = 0; j < h; j++) {
        int a = 2 * j, b = a + 1;
        for (int k = 0; k < m->n; k++) {
            int va = m->m[a][k], vb = m->m[b][k];
            m->m[j][k] = (int8_t)(va == HIST_NONE ? vb : vb == HIST_NONE ? va : (va + vb) / 2);
        }
        m->dt[j] = j ? m->dt[a] + m->dt[b] : m->dt[a];
        m->px[j] = 0.5f * (m->px[a] + m->px[b]);
        m->py[j] = 0.5f * (m->py[a] + m->py[b]);
        m->qx[j] = 0.5f * (m->qx[a] + m->qx[b]);
        m->qy[j] = 0.5f * (m->qy[a] + m->qy[b]);
    }
    m->turn_t /= 2;
    m->T = h;
}

void map_snapshot(map_t *m, const app_t *a)
{
    m->pending += 1.0f;
    // skip while turning, there the body changes the levels, not the place
    if (a->sweep == SWEEP_COUNTDOWN || a->sweep == SWEEP_RUN) return;

    while (m->n < MAP_N) {                       // free slot? take the loudest new AP
        int best = -1;
        for (int i = 0; i < a->n; i++) {
            if (slot_of(m, a->c[i].bssid) >= 0 || a->c[i].rssi < -88.0f) continue;
            if (best < 0 || a->c[i].rssi > a->c[best].rssi) best = i;
        }
        if (best < 0) break;
        int k = m->n++;
        m->id[k] = a->c[best].id;
        memcpy(m->bssid[k], a->c[best].bssid, 6);
        for (int t = 0; t < m->T; t++) m->m[t][k] = HIST_NONE;
        float d = expf((A0 - a->c[best].rssi) / KDB), ang = 2.0f * PI_F * frnd(m);
        float px = m->T ? m->px[m->T - 1] : 0, py = m->T ? m->py[m->T - 1] : 0;
        m->ax[k] = px + d * cosf(ang);
        m->ay[k] = py + d * sinf(ang);
        m->A[k] = A0;
        m->sd[k] = 99.0f;
    }
    if (m->T == MAP_T) decimate(m);
    int t = m->T++;
    m->dt[t] = m->pending;
    m->pending = 0;
    for (int k = 0; k < m->n; k++) {
        int8_t v = HIST_NONE;
        for (int i = 0; i < a->n; i++) {
            const contact_t *c = &a->c[i];
            if (memcmp(c->bssid, m->bssid[k], 6) != 0) continue;
            m->id[k] = c->id;                      // radio id for it, this session only
            v = c->hist[(c->hist_head + HIST_LEN - 1) % HIST_LEN];
        }
        m->m[t][k] = v;
    }
    m->px[t] = t ? m->px[t - 1] : 0;
    m->py[t] = t ? m->py[t - 1] : 0;
    m->qx[t] = t ? m->qx[t - 1] : 0;
    m->qy[t] = t ? m->qy[t - 1] : 0;
    m->seconds++;
}

static float clampf01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static float huber_w(float e)
{
    float ae = fabsf(e);
    return ae <= HUBER ? 1.0f : HUBER / ae;
}

// total cost: huber on the levels + walk smoothness + A prior
static float cost(const map_t *m)
{
    float c = 0;
    for (int t = 0; t < m->T; t++) {
        for (int k = 0; k < m->n; k++) {
            int v = m->m[t][k];
            if (v == HIST_NONE) continue;
            float dx = m->px[t] - m->ax[k], dy = m->py[t] - m->ay[k];
            float e = m->A[k] - 0.5f * KDB * logf(dx * dx + dy * dy + D0SQ) - (float)v;
            float ae = fabsf(e);
            c += ae <= HUBER ? e * e : 2.0f * HUBER * ae - HUBER * HUBER;
        }
        if (t) {
            float dx = m->px[t] - m->px[t - 1], dy = m->py[t] - m->py[t - 1];
            c += SIG2 / (VWALK2 * m->dt[t]) * (dx * dx + dy * dy);
        }
    }
    for (int k = 0; k < m->n; k++) c += LAM_A * (m->A[k] - A0) * (m->A[k] - A0);
    return c;
}

// whole walk in one Gauss-Newton step. each second gives a 2x2 block and smoothness
// ties the neighbours, so its block-tridiagonal: Thomas, forward + back, O(T)
static float s_C[MAP_T][4], s_d[MAP_T][2];

static void path_step(map_t *m, float mu)
{
    int T = m->T;
    for (int t = 0; t < T; t++) {
        float h00 = 0, h01 = 0, h11 = 0, g0 = 0, g1 = 0;
        for (int k = 0; k < m->n; k++) {
            int v = m->m[t][k];
            if (v == HIST_NONE) continue;
            float dx = m->px[t] - m->ax[k], dy = m->py[t] - m->ay[k];
            float q = dx * dx + dy * dy + D0SQ;
            float e = m->A[k] - 0.5f * KDB * logf(q) - (float)v;
            float w = huber_w(e);
            float jx = -KDB * dx / q, jy = -KDB * dy / q;
            h00 += w * jx * jx; h01 += w * jx * jy; h11 += w * jy * jy;
            g0 += w * jx * e;   g1 += w * jy * e;
        }
        float lp = t > 0 ? SIG2 / (VWALK2 * m->dt[t]) : 0.0f;
        float ln = t < T - 1 ? SIG2 / (VWALK2 * m->dt[t + 1]) : 0.0f;
        float b0 = -g0, b1 = -g1;
        if (t > 0) { b0 -= lp * (m->px[t] - m->px[t - 1]); b1 -= lp * (m->py[t] - m->py[t - 1]); }
        if (t < T - 1) { b0 += ln * (m->px[t + 1] - m->px[t]); b1 += ln * (m->py[t + 1] - m->py[t]); }
        float M00 = h00 + mu + lp + ln, M01 = h01, M10 = h01, M11 = h11 + mu + lp + ln;
        if (t > 0) {
            M00 += lp * s_C[t - 1][0]; M01 += lp * s_C[t - 1][1];
            M10 += lp * s_C[t - 1][2]; M11 += lp * s_C[t - 1][3];
            b0 += lp * s_d[t - 1][0];
            b1 += lp * s_d[t - 1][1];
        }
        float det = M00 * M11 - M01 * M10;
        if (fabsf(det) < 1e-9f) det = 1e-9f;
        float i00 = M11 / det, i01 = -M01 / det, i10 = -M10 / det, i11 = M00 / det;
        s_C[t][0] = -ln * i00; s_C[t][1] = -ln * i01;
        s_C[t][2] = -ln * i10; s_C[t][3] = -ln * i11;
        s_d[t][0] = i00 * b0 + i01 * b1;
        s_d[t][1] = i10 * b0 + i11 * b1;
    }
    float x0 = 0, x1 = 0;
    for (int t = T - 1; t >= 0; t--) {
        float n0 = s_d[t][0], n1 = s_d[t][1];
        if (t < T - 1) {
            n0 -= s_C[t][0] * x0 + s_C[t][1] * x1;
            n1 -= s_C[t][2] * x0 + s_C[t][3] * x1;
        }
        x0 = n0; x1 = n1;
        float l = sqrtf(x0 * x0 + x1 * x1), s = l > 3.0f ? 3.0f / l : 1.0f;   // max 3 m per step
        m->px[t] += x0 * s;
        m->py[t] += x1 * s;
    }
}

// 3x3 gauss-jordan with pivoting, 0 if singular
static int solve3(float H[3][3], const float g[3], float x[3])
{
    float a[3][4];
    for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) a[i][j] = H[i][j]; a[i][3] = g[i]; }
    for (int c = 0; c < 3; c++) {
        int p = c;
        for (int r = c + 1; r < 3; r++) if (fabsf(a[r][c]) > fabsf(a[p][c])) p = r;
        if (fabsf(a[p][c]) < 1e-9f) return 0;
        if (p != c) for (int j = 0; j < 4; j++) { float t = a[c][j]; a[c][j] = a[p][j]; a[p][j] = t; }
        for (int r = 0; r < 3; r++) {
            if (r == c) continue;
            float f = a[r][c] / a[c][c];
            for (int j = c; j < 4; j++) a[r][j] -= f * a[c][j];
        }
    }
    for (int i = 0; i < 3; i++) x[i] = a[i][3] / a[i][i];
    return 1;
}

// one AP vs the current walk: position and level at 1 m.
// its doubt comes from the same normal equations
static void ap_step(map_t *m, int k, float mu)
{
    float H[3][3] = { { 0 } }, g[3] = { 0 }, ss = 0, cx = 0, cy = 0;
    int seen = 0;
    for (int t = 0; t < m->T; t++) {
        cx += m->px[t]; cy += m->py[t];
        int v = m->m[t][k];
        if (v == HIST_NONE) continue;
        float dx = m->px[t] - m->ax[k], dy = m->py[t] - m->ay[k];
        float q = dx * dx + dy * dy + D0SQ;
        float e = m->A[k] - 0.5f * KDB * logf(q) - (float)v;
        float w = huber_w(e);
        float J[3] = { KDB * dx / q, KDB * dy / q, 1.0f };
        for (int i = 0; i < 3; i++) {
            g[i] += w * J[i] * e;
            for (int j = 0; j < 3; j++) H[i][j] += w * J[i] * J[j];
        }
        ss += w * e * e;
        seen++;
    }
    if (seen < 6) { m->sd[k] = 99.0f; return; }
    H[2][2] += LAM_A;
    g[2] += LAM_A * (m->A[k] - A0);

    /* doubt: noise = this AP's own misfit, not the hoped 4 dB. x4 because neighbour
     * seconds share errors (walls dont move in a second). plus model error, a straight
     * line through a building: 0.5 m and 30% of the distance from the walk centre */
    float var = ss / (float)(seen > 3 ? seen - 3 : 1);
    if (var < 4.0f) var = 4.0f;
    float cov[3][3], e3[3];
    for (int c = 0; c < 3; c++) {
        float u[3] = { 0, 0, 0 };
        u[c] = 1.0f;
        if (!solve3(H, u, e3)) { m->sd[k] = 99.0f; return; }
        for (int r = 0; r < 3; r++) cov[r][c] = 4.0f * var * e3[r];
    }
    float a = cov[0][0], b = cov[0][1], d = cov[1][1];
    float big = 0.5f * (a + d) + sqrtf(0.25f * (a - d) * (a - d) + b * b);
    cx /= (float)m->T; cy /= (float)m->T;
    float reach = hypotf(m->ax[k] - cx, m->ay[k] - cy) * 0.3f;
    m->sd[k] = sqrtf((big > 0 ? big : 0) + 0.25f + reach * reach);

    for (int i = 0; i < 3; i++) H[i][i] += mu;
    float st[3];
    if (!solve3(H, g, st)) return;
    float l = sqrtf(st[0] * st[0] + st[1] * st[1]), s = l > 3.0f ? 3.0f / l : 1.0f;
    m->ax[k] -= st[0] * s;
    m->ay[k] -= st[1] * s;
    m->A[k] -= st[2] > 6.0f ? 6.0f : st[2] < -6.0f ? -6.0f : st[2];
}

// shifting everything fits the same, so keep the walk centred on 0,0
static void recenter(map_t *m)
{
    float sx = 0, sy = 0;
    for (int t = 0; t < m->T; t++) { sx += m->px[t]; sy += m->py[t]; }
    sx /= (float)m->T; sy /= (float)m->T;
    for (int t = 0; t < m->T; t++) { m->px[t] -= sx; m->py[t] -= sy; }
    for (int k = 0; k < m->n; k++) { m->ax[k] -= sx; m->ay[k] -= sy; }
}

static void run(map_t *m, int iters, float mu)
{
    for (int i = 0; i < iters; i++) {
        path_step(m, mu);
        for (int k = 0; k < m->n; k++) ap_step(m, k, mu);
        recenter(m);
    }
}

// fresh start: each AP at the distance its avg level gives, random direction,
// walk all piled up near 0,0
static void randomize(map_t *m)
{
    for (int k = 0; k < m->n; k++) {
        float s = 0;
        int n = 0;
        for (int t = 0; t < m->T; t++) if (m->m[t][k] != HIST_NONE) { s += m->m[t][k]; n++; }
        float d = n ? expf((A0 - s / (float)n) / KDB) : 10.0f;
        d = d < 1.5f ? 1.5f : d > 25.0f ? 25.0f : d;
        float ang = 2.0f * PI_F * frnd(m);
        m->ax[k] = d * cosf(ang);
        m->ay[k] = d * sinf(ang);
        m->A[k] = A0;
    }
    for (int t = 0; t < m->T; t++) {
        m->px[t] = (frnd(m) - 0.5f) * 0.5f;
        m->py[t] = (frnd(m) - 0.5f) * 0.5f;
    }
}

typedef struct { float px[MAP_T], py[MAP_T], ax[MAP_N], ay[MAP_N], A[MAP_N], sd[MAP_N]; } sol_t;
static sol_t s_best;

static void save(const map_t *m, sol_t *s)
{
    memcpy(s->px, m->px, sizeof s->px); memcpy(s->py, m->py, sizeof s->py);
    memcpy(s->ax, m->ax, sizeof s->ax); memcpy(s->ay, m->ay, sizeof s->ay);
    memcpy(s->A, m->A, sizeof s->A);    memcpy(s->sd, m->sd, sizeof s->sd);
}

static void load(map_t *m, const sol_t *s)
{
    memcpy(m->px, s->px, sizeof s->px); memcpy(m->py, s->py, sizeof s->py);
    memcpy(m->ax, s->ax, sizeof s->ax); memcpy(m->ay, s->ay, sizeof s->ay);
    memcpy(m->A, s->A, sizeof s->A);    memcpy(m->sd, s->sd, sizeof s->sd);
}

void map_solve(map_t *m, int iters)
{
    if (m->T < 12 || m->n < 3) return;
    // lots of local minima. every 20 s a few random restarts compete with the
    // current answer, cheapest one wins
    if (!m->have || (int)m->seconds >= m->next_restart) {
        float best = 1e30f;
        if (m->have) { run(m, 4, 0.5f); best = cost(m); save(m, &s_best); }
        for (int r = 0; r < RESTARTS; r++) {
            randomize(m);
            run(m, 30, 1.0f);
            float c = cost(m);
            if (c < best) { best = c; save(m, &s_best); }
        }
        load(m, &s_best);
        m->have = 1;
        m->next_restart = (int)m->seconds + 20;
    }
    run(m, iters, 0.5f);
    m->cost = cost(m);
}

// ---- published frame ----

typedef struct { float c, s; int mirror; float tx, ty, sx, sy; } xform_t;

static void apply(const xform_t *f, float x, float y, float *ox, float *oy)
{
    x -= f->sx; y -= f->sy;
    if (f->mirror) x = -x;
    *ox = f->c * x - f->s * y + f->tx;
    *oy = f->s * x + f->c * y + f->ty;
}

// weighted procrustes, mirror allowed: rigid move that puts the new solution
// best over the old picture. APs weigh less the more doubt they have
static xform_t procrustes(const map_t *m)
{
    float W = 0, msx = 0, msy = 0, mtx = 0, mty = 0;
    for (int t = 0; t < m->T; t++) { W += 1; msx += m->px[t]; msy += m->py[t]; mtx += m->qx[t]; mty += m->qy[t]; }
    for (int k = 0; k < m->n; k++) {
        float w = 4.0f / (1.0f + m->sd[k] * m->sd[k]);
        W += w; msx += w * m->ax[k]; msy += w * m->ay[k]; mtx += w * m->bx[k]; mty += w * m->by[k];
    }
    msx /= W; msy /= W; mtx /= W; mty /= W;
    float Sxx = 0, Sxy = 0, Syx = 0, Syy = 0;
    for (int i = 0; i < m->T + m->n; i++) {
        float sx, sy, tx, ty, w;
        if (i < m->T) { sx = m->px[i]; sy = m->py[i]; tx = m->qx[i]; ty = m->qy[i]; w = 1; }
        else {
            int k = i - m->T;
            sx = m->ax[k]; sy = m->ay[k]; tx = m->bx[k]; ty = m->by[k];
            w = 4.0f / (1.0f + m->sd[k] * m->sd[k]);
        }
        sx -= msx; sy -= msy; tx -= mtx; ty -= mty;
        Sxx += w * sx * tx; Sxy += w * sx * ty; Syx += w * sy * tx; Syy += w * sy * ty;
    }
    xform_t f;
    float a1 = Sxx + Syy, b1 = Sxy - Syx;           // plain rotation
    float a2 = Syy - Sxx, b2 = -Sxy - Syx;          // mirror first, then rotate
    f.mirror = (a2 * a2 + b2 * b2) > (a1 * a1 + b1 * b1);
    float th = f.mirror ? atan2f(b2, a2) : atan2f(b1, a1);
    f.c = cosf(th); f.s = sinf(th);
    f.sx = msx; f.sy = msy;
    f.tx = mtx; f.ty = mty;
    return f;
}

static void transform_published(map_t *m, float ox, float oy, int mirror, float deg)
{
    for (int i = 0; i < m->T + m->n; i++) {
        float *x = i < m->T ? &m->qx[i] : &m->bx[i - m->T];
        float *y = i < m->T ? &m->qy[i] : &m->by[i - m->T];
        float dx = *x - ox, dy = *y - oy;
        if (mirror) dx = -dx;
        rot_cw(&dx, &dy, deg);
        *x = ox + dx;
        *y = oy + dy;
    }
}

float map_align_turn(map_t *m, const app_t *a)
{
    if (m->T < 1) return -1.0f;
    m->turn_t = m->T - 1;                             // turn bearings are good untill you walk away
    m->turned = 1;
    if (!m->have || !m->published || m->T < 2) return -1.0f;
    float px = m->qx[m->T - 1], py = m->qy[m->T - 1];
    float C[2] = { 0, 0 }, S[2] = { 0, 0 }, W = 0;
    int used = 0;
    for (int i = 0; i < a->n; i++) {
        const contact_t *c = &a->c[i];
        int k = slot_of(m, c->bssid);
        if (k < 0 || !c->has_bearing) continue;
        float dx = m->bx[k] - px, dy = m->by[k] - py, d = sqrtf(dx * dx + dy * dy);
        if (d < 1.0f) continue;                       // too close, angle means nothing
        // weight by both doubts: the turn bearing and the map angle
        float map_sd = atan2f(m->sd[k], d) * (180.0f / PI_F);
        if (map_sd > 50.0f) continue;
        float phi = compass(dx, dy) * (PI_F / 180.0f);
        float beta = c->bearing * (PI_F / 180.0f);
        float w = 1.0f / (c->bearing_sd * c->bearing_sd + map_sd * map_sd);
        for (int r = 0; r < 2; r++) {                 // r = 0 as is, r = 1 mirrored
            float d = beta - (r ? -phi : phi);
            C[r] += w * cosf(d);
            S[r] += w * sinf(d);
        }
        W += w;
        used++;
    }
    if (used < 3) return -(float)used - 10.0f;
    float R0 = sqrtf(C[0] * C[0] + S[0] * S[0]) / W, R1 = sqrtf(C[1] * C[1] + S[1] * S[1]) / W;
    int r = R1 > R0;
    float R = r ? R1 : R0, other = r ? R0 : R1;
    // must agree well AND clearly better than the mirrored way,
    // else left/right stays unknown. no guessing
    if (R < (used >= 5 ? 0.55f : 0.7f) || R - other < 0.15f) return R;
    float th = atan2f(S[r], C[r]) * (180.0f / PI_F);
    transform_published(m, px, py, r, th);
    m->aligned = 1;
    m->heading = 0;                                   // full turn ends facing where it started
    m->heading_ok = 1;
    return r ? R1 : R0;
}

/* did we really walk? compare the solution with "standing still" (each AP at its
 * avg level all the time). a real walk through rooms explains most of the level
 * changes, a radar on a table only what a free path can fit into the noise. */
static float motion_evidence(const map_t *m)
{
    float still = 0, moved = 0;
    for (int k = 0; k < m->n; k++) {
        float s = 0;
        int n = 0;
        for (int t = 0; t < m->T; t++) if (m->m[t][k] != HIST_NONE) { s += m->m[t][k]; n++; }
        if (n < 6) continue;
        float mean = s / (float)n;
        for (int t = 0; t < m->T; t++) {
            int v = m->m[t][k];
            if (v == HIST_NONE) continue;
            float e0 = (float)v - mean;
            float dx = m->px[t] - m->ax[k], dy = m->py[t] - m->ay[k];
            float e1 = m->A[k] - 0.5f * KDB * logf(dx * dx + dy * dy + D0SQ) - (float)v;
            e0 = e0 > HUBER ? HUBER : e0 < -HUBER ? -HUBER : e0;
            e1 = e1 > HUBER ? HUBER : e1 < -HUBER ? -HUBER : e1;
            still += e0 * e0;
            moved += e1 * e1;
        }
    }
    return still > 0 ? clampf01(1.0f - moved / still) : 0.0f;
}

void map_publish(map_t *m, app_t *a)
{
    mapview_t *v = &a->map;
    v->n = (uint8_t)m->n;
    v->T = (int16_t)m->T;
    v->seconds = m->seconds;
    v->active = 0;
    if (!m->have) return;

    if (m->published) {
        xform_t f = procrustes(m);
        for (int t = 0; t < m->T; t++) apply(&f, m->px[t], m->py[t], &m->qx[t], &m->qy[t]);
        for (int k = 0; k < m->n; k++) apply(&f, m->ax[k], m->ay[k], &m->bx[k], &m->by[k]);
    } else {
        memcpy(m->qx, m->px, sizeof m->qx); memcpy(m->qy, m->py, sizeof m->qy);
        memcpy(m->bx, m->ax, sizeof m->bx); memcpy(m->by, m->ay, sizeof m->by);
        m->published = 1;
    }
    // up = the way the walk first went, once it got 1.5 m from the start
    if (!m->framed && !m->aligned) {
        for (int t = 1; t < m->T; t++) {
            float dx = m->qx[t] - m->qx[0], dy = m->qy[t] - m->qy[0];
            if (dx * dx + dy * dy < 1.5f * 1.5f) continue;
            transform_published(m, m->qx[0], m->qy[0], 0, -compass(dx, dy));
            m->framed = 1;
            break;
        }
    }
    /* heading from the last 6 s of walk, but only a real walk: standing still the
     * solved path still shakes by ~1 m and points anywhere. so need >= 2 m, fairly
     * straight (60% of path lenght), and if a turn aligned the map, >= 2.5 m from that spot */
    int T = m->T, k = T - 1;
    float acc = 0, len = 0;
    while (k > 0 && acc < 6.0f) {
        len += hypotf(m->qx[k] - m->qx[k - 1], m->qy[k] - m->qy[k - 1]);
        acc += m->dt[k--];
    }
    float hx = m->qx[T - 1] - m->qx[k], hy = m->qy[T - 1] - m->qy[k];
    float disp = sqrtf(hx * hx + hy * hy);
    int away = !m->aligned || hypotf(m->qx[T - 1] - m->qx[m->turn_t], m->qy[T - 1] - m->qy[m->turn_t]) >= 2.5f;
    if (disp >= 2.0f && disp >= 0.6f * len && away) {
        float h = compass(hx, hy);
        if (m->heading_ok) {                          // smooth: go halfway to the new one
            float d = fmodf(h - m->heading + 540.0f, 360.0f) - 180.0f;
            h = m->heading + 0.5f * d;
            h = fmodf(h + 360.0f, 360.0f);
        }
        m->heading = h;
        m->heading_ok = 1;
    }

    float cx = 0, cy = 0, spread = 0;
    for (int t = 0; t < T; t++) { cx += m->qx[t]; cy += m->qy[t]; }
    cx /= (float)T; cy /= (float)T;
    for (int t = 0; t < T; t++) {
        float d = sqrtf((m->qx[t] - cx) * (m->qx[t] - cx) + (m->qy[t] - cy) * (m->qy[t] - cy));
        if (d > spread) spread = d;
    }
    memcpy(v->id, m->id, sizeof v->id);
    memcpy(v->px, m->qx, sizeof v->px);
    memcpy(v->py, m->qy, sizeof v->py);
    for (int j = 0; j < m->n; j++) {
        v->ax[j] = m->bx[j];
        v->ay[j] = m->by[j];
        v->sd[j] = m->sd[j];
        float dx = m->bx[j] - m->qx[T - 1], dy = m->by[j] - m->qy[T - 1];
        float d = sqrtf(dx * dx + dy * dy);
        // ok for a DIRECTION if its doubt looks small (<= 30 deg) from where you stand
        v->ok[j] = d >= 1.0f && atan2f(m->sd[j], d) <= 30.0f * (PI_F / 180.0f);
    }
    v->heading = m->heading;
    v->heading_ok = (uint8_t)m->heading_ok;
    v->aligned = (uint8_t)m->aligned;
    v->spread = spread;
    v->turned = (uint8_t)m->turned;
    v->since_turn = m->turned ? hypotf(m->qx[T - 1] - m->qx[m->turn_t], m->qy[T - 1] - m->qy[m->turn_t]) : 0.0f;
    // radar lying on a table still "walks" ~1 m in the solution, thats just noise. 3 m is a walk
    v->motion = motion_evidence(m);
    v->active = (uint8_t)(m->framed && spread >= 3.0f && T >= 20 && v->motion >= MOTION_MIN);
}
