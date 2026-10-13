/*
 * preview - the radar screen on PC, same core/ code as the firmware. feeds it fake APs
 * with KNOWN bearings, plays a session (listen, turn 360, track a target), saves PNGs of
 * what the panel shows (dithered RGB565 unpacked back), prints solved bearings vs truth.
 * usage: preview <out-dir> [seed]
 * taps are given relative to the layout, so the same script runs on every display size
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/app.h"
#include "../../core/ui.h"
#include "../../core/ui_int.h"
#include "../../core/paper.h"
#include "preview.h"

/* ---- tiny PNG writer with its own deflate, so no zlib needed ---- */
static uint32_t crc_table[256];
static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static uint32_t crc(uint32_t c, const uint8_t *p, size_t n)
{
    c ^= 0xFFFFFFFFu;
    while (n--) c = crc_table[(c ^ *p++) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t h[8];
    be32(h, len);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t c = crc(0, (const uint8_t *)type, 4);
    c = crc(c, data, len);
    be32(h, c);
    fwrite(h, 1, 4, f);
}
/* deflate with the fixed huffman table (RFC 1951 3.2.6) and plain LZ77, hash chains.
 * not zlib grade, but screenshots shrink 5-20x and it's 80 lines instead of a library */
typedef struct { uint8_t *p; uint32_t bits; int nb; } bitw_t;

static void put_bits(bitw_t *w, uint32_t v, int n)
{
    w->bits |= v << w->nb;
    w->nb += n;
    while (w->nb >= 8) { *w->p++ = (uint8_t)w->bits; w->bits >>= 8; w->nb -= 8; }
}

// huffman codes go out MSB first, the rest of the stream LSB first
static void put_code(bitw_t *w, uint32_t code, int len)
{
    uint32_t r = 0;
    for (int i = 0; i < len; i++) r |= ((code >> i) & 1u) << (len - 1 - i);
    put_bits(w, r, len);
}

static void put_sym(bitw_t *w, int v)
{
    if (v < 144) put_code(w, 0x30u + v, 8);
    else if (v < 256) put_code(w, 0x190u + (v - 144), 9);
    else if (v < 280) put_code(w, (uint32_t)(v - 256), 7);
    else put_code(w, 0xC0u + (v - 280), 8);
}

static const uint16_t LEN_BASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                       67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t  LEN_EXTRA[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t DIST_BASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                        1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t  DIST_EXTRA[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static size_t deflate_fixed(const uint8_t *in, size_t n, uint8_t *out)
{
    enum { WIN = 32768, HBITS = 15, CHAIN = 48 };
    int32_t *head = malloc(sizeof(int32_t) << HBITS), *prev = malloc(sizeof(int32_t) * WIN);
    for (int i = 0; i < 1 << HBITS; i++) head[i] = -1;
    bitw_t w = { out, 0, 0 };
    put_bits(&w, 1, 1);                           // last block
    put_bits(&w, 1, 2);                           // fixed huffman
    size_t i = 0;
    while (i < n) {
        int best = 0, dist = 0;
        if (i + 2 < n) {
            uint32_t h = ((uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2]) * 2654435761u >> (32 - HBITS);
            int32_t c = head[h];
            for (int k = 0; k < CHAIN && c >= 0 && i - (size_t)c <= WIN - 1; k++, c = prev[c % WIN]) {
                int l = 0, lim = n - i < 258 ? (int)(n - i) : 258;
                while (l < lim && in[c + l] == in[i + l]) l++;
                if (l > best) { best = l; dist = (int)(i - (size_t)c); if (l == lim) break; }
            }
            prev[i % WIN] = head[h];
            head[h] = (int32_t)i;
        }
        if (best >= 3) {
            int li = 28;
            while (LEN_BASE[li] > best) li--;
            put_sym(&w, 257 + li);
            put_bits(&w, (uint32_t)(best - LEN_BASE[li]), LEN_EXTRA[li]);
            int di = 29;
            while (DIST_BASE[di] > dist) di--;
            put_code(&w, (uint32_t)di, 5);
            put_bits(&w, (uint32_t)(dist - DIST_BASE[di]), DIST_EXTRA[di]);
            // the skipped bytes go into the hash too, or later matches are missed
            for (size_t j = i + 1; j < i + (size_t)best && j + 2 < n; j++) {
                uint32_t h = ((uint32_t)in[j] << 16 | (uint32_t)in[j + 1] << 8 | in[j + 2]) * 2654435761u >> (32 - HBITS);
                prev[j % WIN] = head[h];
                head[h] = (int32_t)j;
            }
            i += (size_t)best;
        } else {
            put_sym(&w, in[i++]);
        }
    }
    put_sym(&w, 256);
    if (w.nb) put_bits(&w, 0, 8 - w.nb);
    free(head); free(prev);
    return (size_t)(w.p - out);
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

static int write_png(const char *path, const uint8_t *rgb, int w, int h)
{
    // every row gets the filter that leaves the smallest numbers (the usual trick)
    const size_t stride = (size_t)w * 3;
    size_t raw_len = (size_t)h * (1 + stride);
    uint8_t *raw = malloc(raw_len), *tmp = malloc(stride);
    for (int y = 0; y < h; y++) {
        const uint8_t *cur = rgb + y * stride, *up = y ? cur - stride : 0;
        uint8_t *dst = raw + y * (1 + stride);
        long best_sum = -1;
        for (int f = 0; f < 5; f++) {
            if (f >= 2 && !up) break;
            long sum = 0;
            for (size_t x = 0; x < stride; x++) {
                int a = x >= 3 ? cur[x - 3] : 0, b = up ? up[x] : 0, c = (x >= 3 && up) ? up[x - 3] : 0;
                int pred = f == 0 ? 0 : f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) / 2 : paeth(a, b, c);
                tmp[x] = (uint8_t)(cur[x] - pred);
                sum += tmp[x] < 128 ? tmp[x] : 256 - tmp[x];
            }
            if (best_sum < 0 || sum < best_sum) { best_sum = sum; dst[0] = (uint8_t)f; memcpy(dst + 1, tmp, stride); }
        }
    }
    free(tmp);
    uint8_t *z = malloc(raw_len + raw_len / 4 + 64), *p = z;
    *p++ = 0x78; *p++ = 0x01;
    p += deflate_fixed(raw, raw_len, p);
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521u; b = (b + a) % 65521u; }
    be32(p, (b << 16) | a);
    p += 4;
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return 0; }
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)(p - z));
    chunk(f, "IEND", 0, 0);
    fclose(f);
    free(raw); free(z);
    return 1;
}

/* ---- fake neighbourhood ---- */
typedef struct {
    const char *ssid;
    uint8_t ch, sec;
    float bearing, dist_m;   // the truth
    float busy;              // data frames per beacon
} sim_ap_t;

static const sim_ap_t APS[] = {
    { "Kirin82",              6, SEC_WPA2,  40, 2.5f, 3 },
    { "Kirin82-Guest",        6, SEC_WPA2,  42, 2.5f, 0 },
    { "UPC Wi-Free",          1, SEC_OPEN, 200, 9.0f, 0 },
    { "Telekom-8F2A",        11, SEC_WPA3, 300, 7.0f, 1 },
    { "O2-Internet-427",      1, SEC_WPA2, 120, 14.0f, 0 },
    { "Byt_Sokolova_12",      3, SEC_WPA2,  80, 11.0f, 0 },
    { "",                     9, SEC_WPA2, 250, 6.0f, 0 },
    { "Domáca sieť Žilina",   6, SEC_WPA2, 160, 18.0f, 0 },
    { "Квартира 7",          11, SEC_WPA2, 330, 22.0f, 0 },
    { "HP-Print-3B-LaserJet", 6, SEC_WPA,   10, 5.0f, 0 },
    { "DIRECT-roku-112",     13, SEC_WPA2, 230, 25.0f, 0 },
    { "starbucks free",       1, SEC_OPEN,  95, 30.0f, 0 },
    { "Vodafone-A71",         4, SEC_WEP,  275, 28.0f, 0 },
    { "TP-Link_5C1E",        11, SEC_WPA2,  60, 16.0f, 0 },
};
#define NAPS ((int)(sizeof APS / sizeof APS[0]))

static uint32_t rng = 12345;
float frand(void) { rng = rng * 1664525u + 1013904223u; return (float)(rng >> 8) / 16777216.0f; }
float gauss(void) { float u = frand() + 1e-6f, v = frand(); return sqrtf(-2.0f * logf(u)) * cosf(6.2831853f * v); }

// heading at time t: steady turn while sweeping plus +-8 deg wobble like a real hand, else 0
static float facing(const app_t *a, uint32_t t)
{
    if (a->sweep != SWEEP_RUN) return 0.0f;
    float h = (float)(t - a->sweep_t0) * 360.0f / SWEEP_RUN_MS;
    return h + 8.0f * sinf((float)t * 0.0007f);
}

static float g_walk_dir;   // walking direction, deg

static int8_t sim_rssi(const sim_ap_t *ap, float face, int off_channel, float walk)
{
    // walking: APs ahead get closer, the ones behind further away
    float ahead = cosf((ap->bearing - g_walk_dir) * 3.14159265f / 180.0f);
    float d = ap->dist_m * (1.0f - (1.0f - walk) * ahead);
    float p = -38.0f - 28.0f * log10f(d < 0.5f ? 0.5f : d);
    // body shadow, up to ~10 dB with your back to the AP
    float rel = (face - ap->bearing) * 3.14159265f / 180.0f;
    p -= 10.0f * (1.0f - cosf(rel)) * 0.5f;
    p += 3.0f * gauss();                     // fading
    if (off_channel) p -= 14.0f * (float)off_channel;
    if (p > -20.0f) p = -20.0f;
    return (int8_t)lroundf(p);
}

static void make_bssid(int i, uint8_t *m)
{
    static const uint8_t oui[6][3] = { { 0xA4, 0x2B, 0xB0 }, { 0x00, 0x1E, 0x58 }, { 0xD8, 0x07, 0xB6 },
                                       { 0x3C, 0x84, 0x6A }, { 0x50, 0xC7, 0xBF }, { 0xF4, 0xF2, 0x6D } };
    memcpy(m, oui[i % 6], 3);
    m[3] = (uint8_t)(0x10 + i * 7); m[4] = (uint8_t)(0x20 + i * 13); m[5] = (uint8_t)(0x30 + i * 29);
}

// one dwell on a channel: APs on it or next to it beacon ~10 times a second
static void radio_dwell(app_t *a, int ch, uint32_t t0, uint32_t dwell, float walk)
{
    a->channel = (uint8_t)ch;
    for (uint32_t t = t0; t < t0 + dwell; t += 10) {
        for (int i = 0; i < NAPS; i++) {
            const sim_ap_t *ap = &APS[i];
            int off = abs(ap->ch - ch);
            if (off > 2) continue;
            // beacon interval 102.4 ms, each AP with its own phase
            if (((t + (uint32_t)i * 37u) % 102u) >= 10u) continue;
            sample_t s;
            memset(&s, 0, sizeof s);
            make_bssid(i, s.bssid);
            s.rssi = sim_rssi(ap, facing(a, t), off, walk);
            if (s.rssi < -94) continue;
            if (frand() > (s.rssi + 95.0f) / 25.0f) continue;   // weak beacons get lost, like on a real street
            s.ch_rx = (uint8_t)ch;
            s.ch_ap = ap->ch;
            s.kind = SMP_BEACON;
            s.sec = ap->sec;
            s.ssid_len = (uint8_t)strlen(ap->ssid);
            memcpy(s.ssid, ap->ssid, s.ssid_len);
            app_sample(a, &s, t);
            for (int k = 0; k < (int)ap->busy; k++) {
                s.kind = SMP_DATA;
                s.rssi = sim_rssi(ap, facing(a, t), off, walk);
                app_sample(a, &s, t + 1);
            }
        }
        app_tick(a, t);
    }
}

/* ---- render a frame ---- */
#define STRIP 24
static uint32_t strip_px[GFX_W * STRIP];
static uint16_t frame565[GFX_W * GFX_H];
static uint8_t  paper[PAPER_STRIDE * GFX_H];        // e-paper builds: what the panel gets

void render(ui_t *u, const app_t *a, uint32_t now)
{
    u->snap = *a;
    ui_frame(u, now);
    for (int y = 0; y < GFX_H; y += STRIP) {
        canvas_t cv = { strip_px, y, y + STRIP <= GFX_H ? STRIP : GFX_H - y };   // last strip may be short
        ui_strip(u, &cv);
        gfx_pack565(&cv, frame565 + y * GFX_W);
        if (RADAR_EPAPER) paper_pack(&cv, paper);
    }
}

void shot(const char *dir, const char *name, int zoom)
{
    int w = GFX_W * zoom, h = GFX_H * zoom;
    uint8_t *rgb = malloc((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t v = frame565[(y / zoom) * GFX_W + x / zoom];
            v = (uint16_t)((v >> 8) | (v << 8));              // swap bytes back from panel order
            uint8_t *o = rgb + ((size_t)y * w + x) * 3;
            o[0] = (uint8_t)(((v >> 11) & 31) * 255 / 31);
            o[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63);
            o[2] = (uint8_t)((v & 31) * 255 / 31);
#if RADAR_EPAPER
            // e-paper: the 4 grey codes as bright as the panel really makes them, paper a bit warm
            int px = x / zoom, code = (paper[(y / zoom) * PAPER_STRIDE + px / 4] >> (6 - 2 * (px & 3))) & 3;
            int lv = PAPER_LEVEL[code];
            o[0] = (uint8_t)(28 + lv * (232 - 28) / 255);
            o[1] = (uint8_t)(28 + lv * (230 - 28) / 255);
            o[2] = (uint8_t)(32 + lv * (222 - 32) / 255);
#endif
#if RADAR_ROUND
            // round glass: what is outside the circle is not there, show it the way the eye sees it
            float dx = (x + 0.5f) / zoom - GFX_W / 2.0f, dy = (y + 0.5f) / zoom - GFX_H / 2.0f;
            if (dx * dx + dy * dy > (GFX_W / 2.0f) * (GFX_W / 2.0f)) o[0] = o[1] = o[2] = 24;
#endif
        }
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", dir, name);
    if (write_png(path, rgb, w, h)) printf("wrote %s\n", path);
    free(rgb);
}

// fake finger: down, hold, up, then do what ever the ui asked for
void tap(ui_t *u, app_t *a, int x, int y, uint32_t t)
{
    ui_cmd_t cmd;
    memset(&cmd, 0, sizeof cmd);
    u->snap = *a;
    ui_frame(u, t);
    ui_touch(u, 1, x, y, 0, 0, t, &cmd);
    ui_touch(u, 1, x, y, 0, 0, t + 40, &cmd);
    ui_touch(u, 0, x, y, 0, 0, t + 80, &cmd);
    if (cmd.select) app_select(a, cmd.select_id);
    if (cmd.sweep) app_sweep_toggle(a, t);
}

// finger held still 1.3 s, then lifted
static void hold(ui_t *u, app_t *a, int x, int y, uint32_t t)
{
    ui_cmd_t cmd;
    memset(&cmd, 0, sizeof cmd);
    u->snap = *a;
    ui_frame(u, t);
    for (uint32_t d = 0; d <= 1300u; d += 50u) ui_touch(u, 1, x, y, 0, 0, t + d, &cmd);
    ui_touch(u, 0, x, y, 0, 0, t + 1350u, &cmd);
}

// one press on the dial / keys of the e-paper boards
static void key(ui_t *u, app_t *a, int k, uint32_t t)
{
    ui_cmd_t cmd;
    memset(&cmd, 0, sizeof cmd);
    u->snap = *a;
    ui_frame(u, t);
    ui_key(u, k, &cmd);
    if (cmd.select) app_select(a, cmd.select_id);
    if (cmd.sweep) app_sweep_toggle(a, t);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    if (argc > 2) rng = (uint32_t)atoi(argv[2]);
    crc_init();
    static app_t app;
    static ui_t ui;
    app_init(&app, 0);
    if (!ui_init(&ui, 0)) { printf("no memory\n"); return 1; }

    // places to tap, worked out from the layout (on 320x240 they are 270,100 and 302,218 etc)
    const int panel = UI_PX + 42;                       // somewhere in the right panel
    const int last_btn_x = GFX_W - 18, last_btn_y = GFX_H - 22;   // X or HUE, bottom right
    const int chan6 = spectrum_channel_x(6);            // channel 6 on the big spectrum
    const int top = GFX_W / 2, top_y = 20;              // round: top of the glass = back

    // the session script: time in ms, screenshot name, action, and who does it:
    // T square touch screens, R round glass, K the e-paper boards with keys
    enum { T = 1, R = 2, K = 4, ALL = 7 };
    const int mode = RADAR_EPAPER ? K : RADAR_ROUND ? R : T;
    uint32_t t = 0, dwell;
    float walk = 1.0f;
    int walking = 0;
    /* actions: 1 hold BOOT, 2 press BOOT, 3 walk to target, 4 tap at x,y,
     * 5 tap loudest blip, 6 tap empty spot on radar, 7 calibrate, 8 key x, 9 long press at x,y,
     * 10 drop the target (BOOT past the last network, or BACK past it on paper) */
    struct { uint32_t at; const char *name; int action, x, y, who; } plan[] = {
        { 900,   "01-boot",        0, 0, 0, ALL },
        { 9000,  "02-listening",   0, 0, 0, ALL },
        { 9100,  0,                1, 0, 0, ALL },
        { 10500, "03-countdown",   0, 0, 0, ALL },
        { 26500, "04-turning",     0, 0, 0, ALL },
        { 37100, "05-result",      0, 0, 0, ALL },
        { 44000, "06-bearings",    0, 0, 0, ALL },
        { 44100, 0,                5, 0, 0, T | R },          // tap the loudest dot
        { 44100, 0,                8, KEY_DOWN, 0, K },       // or step onto it
        { 52000, "07-target",      0, 0, 0, ALL },
        { 52100, 0,                3, 0, 0, ALL },            // walk towards it
        { 60000, 0,                4, panel, 100, T },        // panel -> hunt view
        { 60000, 0,                6, 0, 0, R },              // empty radar -> hunt view
        { 60000, 0,                8, KEY_OK, 0, K },
        { 62000, "08-hunt",        0, 0, 0, ALL },
        { 62100, 0,                4, 30, 8, T },             // header = back
        { 62100, 0,                4, top, top_y, R },
        { 62100, 0,                8, KEY_BACK, 0, K },
        { 62200, 0,                4, last_btn_x, last_btn_y, T },   // X = no target
        { 62200, 0,               10, 0, 0, R | K },
        { 62300, 0,                4, panel, 30, T },         // count -> list
        { 62300, 0,                6, 0, 0, R },              // empty radar, no target -> list
        { 62300, 0,                8, KEY_MENU, 0, K },       // paper: main -> list (the target screen is skipped, no target)
        { 63000, "09-list",        0, 0, 0, ALL },
        { 63100, 0,                4, 30, 8, T },
        { 63100, 0,                4, top, top_y, R },
        { 63100, 0,                8, KEY_MENU, 0, K },       // list -> spectrum
        { 63200, 0,                4, panel, GFX_H - 65, T }, // small spectrum -> big one
        { 63200, 0,                8, KEY_MENU, 0, R },       // swipe to the list
        { 63250, 0,                8, KEY_MENU, 0, R },       // and on to the spectrum
        { 63300, 0,                4, chan6, 120, T | R },    // channel 6
        { 63300, 0,                8, KEY_OK, 0, K },         // the tuned one
        { 64000, "10-spectrum",    0, 0, 0, ALL },
        { 64100, 0,                4, 30, 8, T },
        { 64100, 0,                4, top, top_y, R },
        { 64100, 0,                8, KEY_MENU, 0, K },       // paper: channels -> walk map
        { 64200, 0,                6, 0, 0, T },              // empty radar -> full screen
        { 65000, "11-radar-full",  0, 0, 0, T },
        { 65000, "11-walk-map",    0, 0, 0, K },
        { 65100, 0,                6, 0, 0, T },
        { 65100, 0,                8, KEY_BACK, 0, K },
        { 65200, 0,                4, last_btn_x, last_btn_y, T },   // HUE, every tap is the next theme
        { 65200, 0,                9, top, GFX_H / 2 - 30, R },      // round: long press on the radar
        { 66000, "12-theme-phosphor", 0, 0, 0, T | R },
        { 66100, 0,                4, last_btn_x, last_btn_y, T },
        { 66100, 0,                9, top, GFX_H / 2 - 30, R },
        { 67000, "13-theme-amber", 0, 0, 0, T | R },
        { 67100, 0,                4, last_btn_x, last_btn_y, T },
        { 67100, 0,                9, top, GFX_H / 2 - 30, R },
        { 68000, "14-theme-crimson", 0, 0, 0, T | R },
        { 68100, 0,                4, last_btn_x, last_btn_y, T },
        { 68100, 0,                9, top, GFX_H / 2 - 30, R },
        { 69000, "15-theme-synth", 0, 0, 0, T | R },
        { 69100, 0,                4, last_btn_x, last_btn_y, T },
        { 69100, 0,                9, top, GFX_H / 2 - 30, R },
        { 69200, 0,                7, 0, 0, T | R },
        { 70000, "16-touch-align", 0, 0, 0, T | R },
    };
    int np = (int)(sizeof plan / sizeof plan[0]);
    for (int k = 0; k < np; k++) {
        if (!(plan[k].who & mode)) continue;
        while (t < plan[k].at) {
            int ch = app_next_channel(&app, &dwell);
            uint32_t d = dwell;
            if (t + d > plan[k].at) d = plan[k].at - t;
            radio_dwell(&app, ch, t, d, walk);
            t += d;
            if (walking && walk > 0.35f) walk -= 0.012f;
            // easing runs per frame, so keep the ui ticking at ~25 fps between shots
            static uint32_t last_frame = 0;
            while (last_frame + 40 <= t) { last_frame += 40; ui.snap = app; ui_frame(&ui, last_frame); }
        }
        if (plan[k].action == 1) app_sweep_toggle(&app, t);
        if (plan[k].action == 2) app_select_next(&app);
        if (plan[k].action == 4) tap(&ui, &app, plan[k].x, plan[k].y, t);
        if (plan[k].action == 5) {
            ui.snap = app;
            ui_frame(&ui, t);
            tap(&ui, &app, (int)ui.b[ui.order[0]].x, (int)ui.b[ui.order[0]].y, t);
        }
        if (plan[k].action == 6) {
            ui.snap = app;
            ui_frame(&ui, t);
            const rgeo_t *g = ui.view == VIEW_RADAR ? &G_FULL : &G_MAIN;
            for (int r = 20; r < 90; r += 7) {             // find a spot with no dot nearby
                int x = g->cx - r, y = g->cy + r / 2, near = 0;
                for (int i = 0; i < ui.n; i++) near |= hypotf(ui.b[i].x - (float)x, ui.b[i].y - (float)y) < 18.0f;
                if (!near) { tap(&ui, &app, x, y, t); break; }
            }
        }
        if (plan[k].action == 7) ui_start_calibration(&ui);
        if (plan[k].action == 8) key(&ui, &app, plan[k].x, t);
        if (plan[k].action == 9) hold(&ui, &app, plan[k].x, plan[k].y, t);
        if (plan[k].action == 10) app_select(&app, 0);
        if (plan[k].action == 3) {
            walking = 1;
            for (int i = 0; i < NAPS; i++) {
                uint8_t m[6];
                make_bssid(i, m);
                const contact_t *tc = app_find(&app, app.target_id);
                if (tc && !memcmp(tc->bssid, m, 6)) g_walk_dir = APS[i].bearing;
            }
        }
        if (plan[k].name) {
            render(&ui, &app, t);
            shot(dir, plan[k].name, 2);
        }
    }

    printf("\nbearing check (truth vs solved):\n");
    double se = 0; int nres = 0;
    for (int i = 0; i < NAPS; i++) {
        uint8_t m[6];
        make_bssid(i, m);
        for (int j = 0; j < app.n; j++) {
            const contact_t *c = &app.c[j];
            if (memcmp(c->bssid, m, 6)) continue;
            if (c->has_bearing) {
                float err = fmodf(c->bearing - APS[i].bearing + 540.0f, 360.0f) - 180.0f;
                printf("  %-22s true %3.0f  got %5.1f +-%4.1f  err %+6.1f  swing %4.1f dB\n",
                       APS[i].ssid[0] ? APS[i].ssid : "[hidden]", APS[i].bearing, c->bearing, c->bearing_sd, err, c->swing_db);
                se += err * err; nres++;
            } else {
                printf("  %-22s true %3.0f  unresolved             swing %4.1f dB\n",
                       APS[i].ssid[0] ? APS[i].ssid : "[hidden]", APS[i].bearing, c->swing_db);
            }
        }
    }
    if (nres) printf("  rms error %.1f deg over %d resolved\n", sqrt(se / nres), nres);
    walk_main(dir, 0);
    return walk_main(dir, 1);
}
