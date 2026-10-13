#include "flashstore.h"

#include <stdlib.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"

static const char *TAG = "store";

#define SLOT_BYTES (12 * 1024)
#define SLOT_MAGIC 0x544F4C53u            // "SLOT" in memory (little endian)

typedef struct {
    uint32_t magic, seq, len, crc;
} slot_hdr_t;

static const esp_partition_t *s_part;
static int      s_slots, s_next;
static uint32_t s_seq;

int fstore_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "radar");
    if (!s_part) { ESP_LOGW(TAG, "no \"radar\" partition: nothing will be kept"); return 0; }
    s_slots = (int)(s_part->size / SLOT_BYTES);
    int newest = -1;
    for (int i = 0; i < s_slots; i++) {
        slot_hdr_t h;
        if (esp_partition_read(s_part, (size_t)i * SLOT_BYTES, &h, sizeof h) != ESP_OK) continue;
        if (h.magic != SLOT_MAGIC || h.len > SLOT_BYTES - sizeof h) continue;
        if (newest < 0 || h.seq > s_seq) { s_seq = h.seq; newest = i; }
    }
    s_next = newest < 0 ? 0 : (newest + 1) % s_slots;     // next save goes after the newest
    ESP_LOGI(TAG, "%d slots of %d KB, newest save #%u", s_slots, SLOT_BYTES / 1024, (unsigned)s_seq);
    return 1;
}

// load the newest slot with good crc. if that one is broken try the one before, and so on
int fstore_load(map_t *m, saved_net_t *nets, int *n)
{
    *n = 0;
    if (!s_part) return 0;
    uint8_t *buf = malloc(SLOT_BYTES);
    if (!buf) return 0;
    uint32_t below = UINT32_MAX;
    int ok = 0;
    for (int tries = 0; tries < s_slots && !ok; tries++) {
        int best = -1;
        uint32_t seq = 0;
        slot_hdr_t bh = { 0 };
        for (int i = 0; i < s_slots; i++) {
            slot_hdr_t h;
            if (esp_partition_read(s_part, (size_t)i * SLOT_BYTES, &h, sizeof h) != ESP_OK) continue;
            if (h.magic != SLOT_MAGIC || h.len > SLOT_BYTES - sizeof h || h.seq >= below) continue;
            if (best < 0 || h.seq > seq) { best = i; seq = h.seq; bh = h; }
        }
        if (best < 0) break;
        below = seq;
        if (esp_partition_read(s_part, (size_t)best * SLOT_BYTES + sizeof bh, buf, bh.len) != ESP_OK) continue;
        if (esp_rom_crc32_le(0, buf, bh.len) != bh.crc) { ESP_LOGW(TAG, "save #%u is damaged", (unsigned)seq); continue; }
        ok = store_unpack(buf, bh.len, m, nets, n);
        if (ok) ESP_LOGI(TAG, "restored save #%u (%u bytes)", (unsigned)seq, (unsigned)bh.len);
    }
    free(buf);
    return ok;
}

/* data first, header last. if power dies in the middle this slot has no header,
 * so the previous save is still the newest valid one */
int fstore_save(const map_t *m, const saved_net_t *nets, int n)
{
    if (!s_part) return 0;
    size_t len = store_size(n);
    if (len > SLOT_BYTES - sizeof(slot_hdr_t)) return 0;
    uint8_t *buf = malloc(len);
    if (!buf) return 0;
    store_pack(buf, len, m, nets, n);
    slot_hdr_t h = { SLOT_MAGIC, s_seq + 1, (uint32_t)len, esp_rom_crc32_le(0, buf, len) };
    size_t at = (size_t)s_next * SLOT_BYTES;
    int ok = esp_partition_erase_range(s_part, at, SLOT_BYTES) == ESP_OK &&
             esp_partition_write(s_part, at + sizeof h, buf, len) == ESP_OK &&
             esp_partition_write(s_part, at, &h, sizeof h) == ESP_OK;
    free(buf);
    if (ok) {
        s_seq++;
        s_next = (s_next + 1) % s_slots;
    }
    return ok;
}
