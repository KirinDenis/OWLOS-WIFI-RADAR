#include "store.h"
#include <string.h>

#define STORE_MAGIC   0x52445253u        // "SRDR" in memory (little endian)
#define STORE_VERSION 1u
#define SESSION_GAP_S 30.0f              // pretend 30 s passed, new walk is only loosely tied to the old one

// magic, version and sizes get checked on load, state from another build is refused
typedef struct {
    uint32_t magic, version, map_size, net_size, nets;
} header_t;

size_t store_size(int n)
{
    return sizeof(header_t) + sizeof(map_t) + (size_t)n * sizeof(saved_net_t);
}

size_t store_pack(uint8_t *buf, size_t cap, const map_t *m, const saved_net_t *nets, int n)
{
    size_t need = store_size(n);
    if (cap < need) return 0;
    header_t h = { STORE_MAGIC, STORE_VERSION, (uint32_t)sizeof *m, (uint32_t)sizeof *nets, (uint32_t)n };
    memcpy(buf, &h, sizeof h);
    memcpy(buf + sizeof h, m, sizeof *m);
    if (n) memcpy(buf + sizeof h + sizeof *m, nets, (size_t)n * sizeof *nets);
    return need;
}

int store_unpack(const uint8_t *buf, size_t len, map_t *m, saved_net_t *nets, int *n)
{
    header_t h;
    *n = 0;
    if (len < sizeof h) return 0;
    memcpy(&h, buf, sizeof h);
    if (h.magic != STORE_MAGIC || h.version != STORE_VERSION || h.map_size != sizeof *m ||
        h.net_size != sizeof *nets || h.nets > MAX_CONTACTS || len < store_size((int)h.nets))
        return 0;
    const map_t *src = (const map_t *)(const void *)(buf + sizeof h);
    if (src->n < 0 || src->n > MAP_N || src->T < 0 || src->T > MAP_T) return 0;   // sanity check before we copy
    memcpy(m, buf + sizeof h, sizeof *m);
    if (h.nets) memcpy(nets, buf + sizeof h + sizeof *m, h.nets * sizeof *nets);
    memset(m->id, 0, sizeof m->id);             // ids are new now, map_snapshot fills them again
    m->pending = SESSION_GAP_S;
    m->heading_ok = 0;                          // could be facing anywhere now
    m->turned = 0;                              // last turn was made somewhere else
    m->next_restart = (int)m->seconds + 10;     // restarts soon, we may stand somewhere new
    *n = (int)h.nets;
    return 1;
}

int store_save(FILE *f, const map_t *m, const saved_net_t *nets, int n)
{
    static uint8_t buf[sizeof(header_t) + sizeof(map_t) + MAX_CONTACTS * sizeof(saved_net_t)];
    size_t len = store_pack(buf, sizeof buf, m, nets, n);
    return len && fwrite(buf, 1, len, f) == len;
}

int store_load(FILE *f, map_t *m, saved_net_t *nets, int *n)
{
    static uint8_t buf[sizeof(header_t) + sizeof(map_t) + MAX_CONTACTS * sizeof(saved_net_t)];
    size_t len = fread(buf, 1, sizeof buf, f);
    return store_unpack(buf, len, m, nets, n);
}

int store_collect(const app_t *a, saved_net_t *nets)
{
    for (int i = 0; i < a->n; i++) {
        const contact_t *c = &a->c[i];
        saved_net_t *s = &nets[i];
        memcpy(s->bssid, c->bssid, 6);
        memcpy(s->ssid, c->ssid, sizeof s->ssid);
        s->channel = c->channel;
        s->sec = c->sec;
        s->hidden = c->hidden;
        s->rssi = (int8_t)(c->rssi < -127.0f ? -127 : c->rssi);
        s->peak = c->rssi_peak;
    }
    return a->n;
}

void store_restore(app_t *a, const saved_net_t *nets, int n, uint32_t now)
{
    for (int i = 0; i < n && a->n < MAX_CONTACTS; i++) {
        const saved_net_t *s = &nets[i];
        int known = 0;
        for (int j = 0; j < a->n && !known; j++) known = !memcmp(a->c[j].bssid, s->bssid, 6);
        if (known) continue;
        contact_t *c = &a->c[a->n++];
        memset(c, 0, sizeof *c);
        c->id = a->next_id++;
        memcpy(c->bssid, s->bssid, 6);
        memcpy(c->ssid, s->ssid, sizeof c->ssid);
        c->ssid[32] = 0;
        c->channel = s->channel;
        c->sec = s->sec;
        c->hidden = s->hidden;
        c->rssi = c->rssi_slow = (float)s->rssi;
        c->rssi_last = s->rssi;
        c->rssi_peak = s->peak;
        // remembered, not heard: drawn faded, gone in ~30 s if nothing is recieved.
        // n == 0 so the first real frame sets the level
        c->first_ms = c->last_ms = now - 15000u;
        for (int k = 0; k < HIST_LEN; k++) c->hist[k] = HIST_NONE;
    }
}

// quoted, inner quotes doubled like csv wants
static void csv_text(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"') fputc('"', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

void store_csv(FILE *f, const saved_net_t *nets, int n)
{
    static const char *sec[] = { "OPEN", "WEP", "WPA", "WPA2", "WPA3" };
    fputs("bssid,ssid,channel,security,rssi_dbm,peak_dbm\r\n", f);
    for (int i = 0; i < n; i++) {
        const saved_net_t *s = &nets[i];
        fprintf(f, "%02x:%02x:%02x:%02x:%02x:%02x,", s->bssid[0], s->bssid[1], s->bssid[2],
                s->bssid[3], s->bssid[4], s->bssid[5]);
        csv_text(f, s->hidden ? "" : s->ssid);
        fprintf(f, ",%d,%s,%d,%d\r\n", s->channel, s->sec <= SEC_WPA3 ? sec[s->sec] : "?", s->rssi, s->peak);
    }
}
