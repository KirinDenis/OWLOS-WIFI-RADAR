#include "app.h"
#include <math.h>
#include <string.h>

#define EXPIRE_MS   45000u    // drop a contact not heard this long
#define TAU_FAST_MS 1500.0f
#define TAU_SLOW_MS 12000.0f

#define DEG (3.14159265f / 180.0f)

// careful: signed on purpose, a sample stamped just after `now` would look 49 days old
static inline int32_t since(uint32_t now, uint32_t t) { return (int32_t)(now - t); }

void app_init(app_t *a, uint32_t now)
{
    memset(a, 0, sizeof *a);
    a->next_id = 1;
    a->hist_sec = now / 1000u;
    a->rate_t0 = now;
    a->channel = 1;
}

static contact_t *find_bssid(app_t *a, const uint8_t *bssid)
{
    for (int i = 0; i < a->n; i++)
        if (memcmp(a->c[i].bssid, bssid, 6) == 0) return &a->c[i];
    return 0;
}

const contact_t *app_find(const app_t *a, uint32_t id)
{
    if (!id) return 0;
    for (int i = 0; i < a->n; i++)
        if (a->c[i].id == id) return &a->c[i];
    return 0;
}

// table full: kick out the worst one (rssi minus age), never the tracked target.
// if the new one is weaker than that it just doesnt get in
static contact_t *add_contact(app_t *a, const sample_t *s, uint32_t now)
{
    contact_t *c;
    if (a->n < MAX_CONTACTS) {
        c = &a->c[a->n++];
    } else {
        int victim = -1;
        float worst = 1e9f;
        for (int i = 0; i < a->n; i++) {
            if (a->c[i].id == a->target_id) continue;
            float score = a->c[i].rssi - (float)since(now, a->c[i].last_ms) * 0.01f;
            if (score < worst) { worst = score; victim = i; }
        }
        if (victim < 0 || worst > (float)s->rssi) return 0;
        c = &a->c[victim];
    }
    memset(c, 0, sizeof *c);
    c->id = a->next_id++;
    if (!a->next_id) a->next_id = 1;
    memcpy(c->bssid, s->bssid, 6);
    c->rssi = c->rssi_slow = (float)s->rssi;
    c->rssi_peak = s->rssi;
    c->first_ms = c->last_ms = now;
    for (int i = 0; i < HIST_LEN; i++) c->hist[i] = HIST_NONE;
    return c;
}

static float smooth(float v, float x, float dt, float tau)
{
    float k = dt / (tau + dt);
    return v + (x - v) * k;
}

void app_sample(app_t *a, const sample_t *s, uint32_t now)
{
    contact_t *c = find_bssid(a, s->bssid);
    if (!c) {
        // only a beacon has name and channel, so data from an unknown AP
        // is ignored until we hear its beacon
        if (s->kind != SMP_BEACON) return;
        c = add_contact(a, s, now);
        if (!c) return;
    }
    if (s->kind == SMP_BEACON) {
        int real = 0;
        for (int i = 0; i < s->ssid_len; i++) if (s->ssid[i]) { real = 1; break; }
        c->hidden = !real && !c->ssid[0];     // a probe response may have named it already
        if (real) {
            memcpy(c->ssid, s->ssid, s->ssid_len);
            c->ssid[s->ssid_len] = 0;
        }
        if (s->ch_ap >= 1 && s->ch_ap <= 14) c->channel = s->ch_ap;
        else if (!c->channel) c->channel = s->ch_rx;
        c->sec = s->sec;
    }
    // 2.4 GHz channels overlap, an AP on 6 is still heard (weaker) when tuned to 4.
    // only frames on the AP's own channel give the real level
    if (s->ch_rx != c->channel) return;

    float x = (float)s->rssi;
    float dt = c->n ? (float)since(now, c->last_ms) : 0.0f;
    if (dt < 0.0f) dt = 0.0f;
    if (c->n == 0) {
        c->rssi = c->rssi_slow = x;
    } else {
        c->rssi = smooth(c->rssi, x, dt + 1.0f, TAU_FAST_MS);
        c->rssi_slow = smooth(c->rssi_slow, x, dt + 1.0f, TAU_SLOW_MS);
    }
    c->rssi_last = s->rssi;
    if (s->rssi > c->rssi_peak) c->rssi_peak = s->rssi;
    c->last_ms = now;
    c->n++;
    c->sec_acc += x;
    if (c->sec_n < 255) c->sec_n++;
    a->pkts++;

    if (a->sweep == SWEEP_RUN) {               // put the frame into its 15 deg slice of the turn
        int bin = (int)(app_heading(a, now) / (360.0f / BEAR_BINS));
        if (bin >= 0 && bin < BEAR_BINS && c->bin_n[bin] < 255) {
            c->bin_sum[bin] += x;
            c->bin_n[bin]++;
        }
    }
}

float app_heading(const app_t *a, uint32_t now)
{
    if (a->sweep != SWEEP_RUN) return 0.0f;
    float h = (float)(now - a->sweep_t0) * 360.0f / (float)SWEEP_RUN_MS;
    return h < 0.0f ? 0.0f : (h > 359.99f ? 359.99f : h);
}

/* body shadow fit: rssi(h) ~ a + p cos h + q sin h, least squares over the slices
 * that heard the AP, weighted by frames per slice. a weak AP is heard only a few times
 * in 24 s, so the circle does not have to be full, just no huge gaps.
 * phi = atan2(q, p) is the peak, swing vs noise says how sure we are. */
static void solve_bearing(contact_t *c, uint32_t now)
{
    float m[BEAR_BINS];
    int filled = 0, total = 0;
    for (int k = 0; k < BEAR_BINS; k++)
        if (c->bin_n[k]) { m[k] = c->bin_sum[k] / c->bin_n[k]; filled++; total += c->bin_n[k]; }
    if (filled < 5 || total < 6) return;

    int gap = 0, run = 0;                          // widest gap with nothing heard, 2 rounds for the wrap
    for (int k = 0; k < 2 * BEAR_BINS; k++) {
        if (c->bin_n[k % BEAR_BINS]) run = 0;
        else if (++run > gap) gap = run;
    }
    if (gap * (360 / BEAR_BINS) > 120) return;      // over 120 deg blind, no way to see a lobe

    double A[3][3] = { { 0 } }, y[3] = { 0 };
    for (int k = 0; k < BEAR_BINS; k++) {
        if (!c->bin_n[k]) continue;
        double th = ((double)k + 0.5) * (360.0 / BEAR_BINS) * DEG, w = c->bin_n[k];
        double f[3] = { 1.0, cos(th), sin(th) };
        for (int i = 0; i < 3; i++) {
            y[i] += w * f[i] * m[k];
            for (int j = 0; j < 3; j++) A[i][j] += w * f[i] * f[j];
        }
    }
    // invert the 3x3 normal matrix, it's inverse is also the covariance (used below)
    double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
               + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (fabs(det) < 1e-9) return;
    double I[3][3];
    for (int i = 0; i < 3; i++)                   // cyclic index trick, signs come out by themselves (3x3 only)
        for (int j = 0; j < 3; j++) {
            int r0 = (j + 1) % 3, r1 = (j + 2) % 3, c0 = (i + 1) % 3, c1 = (i + 2) % 3;
            I[i][j] = (A[r0][c0] * A[r1][c1] - A[r0][c1] * A[r1][c0]) / det;
        }
    double x[3];
    for (int i = 0; i < 3; i++) x[i] = I[i][0] * y[0] + I[i][1] * y[1] + I[i][2] * y[2];
    double p = x[1], q = x[2], b = sqrt(p * p + q * q);
    double phi = atan2(q, p);

    double ss = 0;
    for (int k = 0; k < BEAR_BINS; k++) {
        if (!c->bin_n[k]) continue;
        double th = ((double)k + 0.5) * (360.0 / BEAR_BINS) * DEG;
        double r = m[k] - (x[0] + p * cos(th) + q * sin(th));
        ss += c->bin_n[k] * r * r;
    }
    double var = ss / (double)(total > 3 ? total - 3 : 1);
    if (var < 9.0) var = 9.0;                     // never trust one frame better than 3 dB
    double vphi = var * (p * p * I[2][2] + q * q * I[1][1] - 2.0 * p * q * I[1][2]) / (b * b * b * b + 1e-9);
    double sd = sqrt(vphi > 0 ? vphi : 0) / DEG;

    c->swing_db = (float)(2.0 * b);
    if (b < 1.5 || sd > 60.0) return;             // no lobe worth trusting
    double deg = phi / DEG;
    if (deg < 0) deg += 360.0;
    c->bearing = (float)deg;
    // even a steady hand in a clean room leaves ~15 deg of doubt, so floor it there
    c->bearing_sd = sd < 15.0 ? 15.0f : (float)sd;
    c->has_bearing = 1;
    (void)now;
}

// one value per second per contact: avg of that second, or smoothed rssi if heard in last 5 s
static void roll_history(app_t *a, uint32_t now)
{
    uint32_t sec = now / 1000u;
    int steps = 0;
    while (a->hist_sec < sec && steps++ < HIST_LEN) {
        a->hist_sec++;
        for (int i = 0; i < a->n; i++) {
            contact_t *c = &a->c[i];
            int8_t v = HIST_NONE;
            if (c->sec_n) v = (int8_t)lroundf(c->sec_acc / c->sec_n);
            else if (since(now, c->last_ms) < 5000) v = (int8_t)lroundf(c->rssi);
            c->hist[c->hist_head] = v;
            c->hist_head = (uint8_t)((c->hist_head + 1) % HIST_LEN);
            c->sec_acc = 0;
            c->sec_n = 0;
        }
    }
    a->hist_sec = sec;
}

void app_tick(app_t *a, uint32_t now)
{
    roll_history(a, now);

    if (now - a->rate_t0 >= 1000u) {
        uint32_t d = a->pkts - a->rate_pkts;
        a->pkt_rate = (uint16_t)(d * 1000u / (now - a->rate_t0));
        a->rate_pkts = a->pkts;
        a->rate_t0 = now;
    }

    // expire silent ones, but not mid-turn: the bins live inside the contact
    if (a->sweep != SWEEP_RUN) {
        for (int i = 0; i < a->n; ) {
            if (since(now, a->c[i].last_ms) > (int32_t)EXPIRE_MS) {
                if (a->c[i].id == a->target_id) a->target_id = 0;
                a->c[i] = a->c[--a->n];
            } else {
                i++;
            }
        }
    }

    switch (a->sweep) {
    case SWEEP_COUNTDOWN:                       // countdown done -> clear bins, turn starts
        if (now - a->sweep_t0 >= SWEEP_COUNTDOWN_MS) {
            for (int i = 0; i < a->n; i++) {
                memset(a->c[i].bin_sum, 0, sizeof a->c[i].bin_sum);
                memset(a->c[i].bin_n, 0, sizeof a->c[i].bin_n);
            }
            a->sweep = SWEEP_RUN;
            a->sweep_t0 = now;
        }
        break;
    case SWEEP_RUN:
        if (now - a->sweep_t0 >= SWEEP_RUN_MS) {
            int got = 0;
            for (int i = 0; i < a->n; i++) {
                a->c[i].has_bearing = 0;
                solve_bearing(&a->c[i], now);
                got += a->c[i].has_bearing;
            }
            a->sweep_resolved = (uint8_t)got;
            a->sweep_total = (uint8_t)a->n;
            a->bearing_ms = now;
            a->sweep = SWEEP_RESULT;
            a->sweep_t0 = now;
        }
        break;
    case SWEEP_RESULT:
        if (now - a->sweep_t0 >= SWEEP_RESULT_MS) a->sweep = SWEEP_IDLE;
        break;
    default:
        break;
    }
}

void app_sweep_toggle(app_t *a, uint32_t now)
{
    if (a->sweep == SWEEP_COUNTDOWN || a->sweep == SWEEP_RUN) {
        a->sweep = SWEEP_IDLE;                    // abort, old bearings stay
    } else {
        a->sweep = SWEEP_COUNTDOWN;
        a->sweep_t0 = now;
    }
}

int app_sorted(const app_t *a, uint8_t *idx)
{
    int n = a->n;
    for (int i = 0; i < n; i++) idx[i] = (uint8_t)i;
    for (int i = 1; i < n; i++) {               // insertion sort, n <= 40
        uint8_t v = idx[i];
        int j = i - 1;
        while (j >= 0 && a->c[idx[j]].rssi < a->c[v].rssi) { idx[j + 1] = idx[j]; j--; }
        idx[j + 1] = v;
    }
    return n;
}

void app_select_next(app_t *a)
{
    if (a->sweep == SWEEP_COUNTDOWN || a->sweep == SWEEP_RUN) return;   // no target change mid-turn
    if (a->sweep == SWEEP_RESULT) a->sweep = SWEEP_IDLE;
    uint8_t idx[MAX_CONTACTS];
    int n = app_sorted(a, idx);
    if (!n) { a->target_id = 0; return; }
    int at = -1;
    for (int i = 0; i < n; i++) if (a->c[idx[i]].id == a->target_id) at = i;
    a->target_id = (at + 1 < n) ? a->c[idx[at + 1]].id : 0;
}

// log-distance path loss: -38 dBm at 1 m, exponent 2.8 for a flat with walls.
// only the order of magnitude is right, thats why it gets shown with a tilde
float app_distance_m(float rssi)
{
    return powf(10.0f, (-38.0f - rssi) / 28.0f);
}

// least squares slope over the last 10 s of history, in dB per 10 s
float app_trend(const contact_t *c)
{
    float sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (int i = 0; i < 10; i++) {
        int k = (c->hist_head - 1 - i + HIST_LEN * 2) % HIST_LEN;
        if (c->hist[k] == HIST_NONE) continue;
        float x = (float)-i, y = (float)c->hist[k];
        sx += x; sy += y; sxx += x * x; sxy += x * y;
        n++;
    }
    if (n < 6) return 0.0f;
    float den = n * sxx - sx * sx;
    if (den < 1e-3f) return 0.0f;
    return (n * sxy - sx * sy) / den * 10.0f;
}

/* --- channel plan ---
 *  tracking: 3 long looks at the target's channel, then 1 other one
 *  turning:  only channels where something lives, so each AP is heard as often as possible
 *  else:     survey 1..13, stay longer where we already know networks */
int app_next_channel(app_t *a, uint32_t *dwell_ms)
{
    uint16_t known = 0;
    for (int i = 0; i < a->n; i++)
        if (a->c[i].channel >= 1 && a->c[i].channel <= 13) known |= (uint16_t)(1u << a->c[i].channel);

    const contact_t *t = app_find(a, a->target_id);
    if (t && t->channel && a->sweep != SWEEP_RUN) {
        if (a->hop_sub++ % 4 != 3) {
            *dwell_ms = 150;
            return t->channel;
        }
    }
    if (a->sweep == SWEEP_RUN && known) {
        for (int k = 0; k < 13; k++) {
            a->hop_idx = (uint8_t)(a->hop_idx % 13 + 1);
            if (known & (1u << a->hop_idx)) break;
        }
        *dwell_ms = 110;
        return a->hop_idx;
    }
    a->hop_idx = (uint8_t)(a->hop_idx % 13 + 1);
    *dwell_ms = (known & (1u << a->hop_idx)) ? 120 : 60;
    return a->hop_idx;
}

void app_select(app_t *a, uint32_t id)
{
    if (a->sweep == SWEEP_COUNTDOWN || a->sweep == SWEEP_RUN) return;   // same, not while turning
    if (a->sweep == SWEEP_RESULT) a->sweep = SWEEP_IDLE;
    a->target_id = (id && app_find(a, id)) ? id : 0;
}
