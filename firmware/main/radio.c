/*
 * radio - listen only, we never transmit (no probe requests, no association).
 * promiscuous, one channel at a time. keeps beacons + probe responses (name, channel,
 * security, rssi) and FromDS data frames (only bssid + rssi). busy APs send those way more
 * often than beacons, so a near router reads faster and steadier. payloads never looked at.
 */
#include "radio.h"
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "radio";

static QueueHandle_t     s_q;
static app_t            *s_app;
static SemaphoreHandle_t s_lock;
static volatile uint32_t s_dropped;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// RSN tag -> WPA2, or WPA3 if SAE (or OWE) key management is offered
static uint8_t rsn_security(const uint8_t *ie, int len)
{
    int off = 6;                                   // skip version + group cipher
    if (off + 2 > len) return SEC_WPA2;
    int pc = ie[off] | ie[off + 1] << 8;
    off += 2 + 4 * pc;
    if (off + 2 > len) return SEC_WPA2;
    int ac = ie[off] | ie[off + 1] << 8;
    off += 2;
    for (int i = 0; i < ac && off + 4 <= len; i++, off += 4) {
        if (ie[off] == 0x00 && ie[off + 1] == 0x0F && ie[off + 2] == 0xAC) {
            uint8_t t = ie[off + 3];
            if (t == 8 || t == 9 || t == 18 || t == 24 || t == 25) return SEC_WPA3;
        }
    }
    return SEC_WPA2;
}

static void parse_beacon(const uint8_t *p, int len, sample_t *s)
{
    uint16_t cap = (uint16_t)(p[34] | p[35] << 8);
    int rsn = -1, wpa = 0;
    s->ssid_len = 0;
    s->ch_ap = 0;
    for (int off = 36; off + 2 <= len;) {
        int id = p[off], n = p[off + 1];
        const uint8_t *d = p + off + 2;
        if (off + 2 + n > len) break;
        if (id == 0 && n <= 32) {
            memcpy(s->ssid, d, (size_t)n);
            s->ssid_len = (uint8_t)n;
        } else if (id == 3 && n >= 1) {
            s->ch_ap = d[0];
        } else if (id == 48) {
            rsn = rsn_security(d, n);
        } else if (id == 221 && n >= 4 && d[0] == 0x00 && d[1] == 0x50 && d[2] == 0xF2 && d[3] == 1) {
            wpa = 1;                                // MS vendor tag = old WPA1
        }
        off += 2 + n;
    }
    if (rsn >= 0) s->sec = (uint8_t)rsn;
    else if (wpa) s->sec = SEC_WPA;
    else if (cap & 0x0010) s->sec = SEC_WEP;
    else s->sec = SEC_OPEN;
}

// NOTE: runs inside the wifi task, keep it short. radio_task does the rest
static void rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *pkt = buf;
    const uint8_t *p = pkt->payload;
    int len = (int)pkt->rx_ctrl.sig_len - 4;       // sig_len includes the 4 byte FCS
    if (len < 24) return;
    int ftype = (p[0] >> 2) & 3, sub = (p[0] >> 4) & 15;
    sample_t s;
    s.rssi = (int8_t)pkt->rx_ctrl.rssi;
    s.ch_rx = (uint8_t)pkt->rx_ctrl.channel;
    if (type == WIFI_PKT_MGMT && ftype == 0 && (sub == 8 || sub == 5)) {
        if (len < 36) return;
        memcpy(s.bssid, p + 16, 6);                 /* addr3 = BSSID */
        s.kind = SMP_BEACON;
        parse_beacon(p, len, &s);
    } else if (type == WIFI_PKT_DATA && ftype == 2 && (p[1] & 3) == 2) {
        memcpy(s.bssid, p + 10, 6);                 /* FromDS: addr2 = BSSID */
        s.kind = SMP_DATA;
        s.ch_ap = 0;
        s.sec = 0;
        s.ssid_len = 0;
    } else {
        return;
    }
    if (xQueueSend(s_q, &s, 0) != pdTRUE) s_dropped++;
}

static void radio_task(void *arg)
{
    (void)arg;
    uint32_t next_hop = 0, next_tick = 0;
    for (;;) {
        uint32_t now = now_ms();
        if ((int32_t)(now - next_hop) >= 0) {
            uint32_t dwell;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            int ch = app_next_channel(s_app, &dwell);
            s_app->channel = (uint8_t)ch;
            xSemaphoreGive(s_lock);
            esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);
            next_hop = now + dwell;
        }
        int32_t wait = (int32_t)(next_hop - now);
        if (wait > 20) wait = 20;
        if (wait < 1) wait = 1;
        sample_t s;
        if (xQueueReceive(s_q, &s, pdMS_TO_TICKS(wait)) == pdTRUE) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            int n = 0;
            do {
                app_sample(s_app, &s, now_ms());
            } while (++n < 64 && xQueueReceive(s_q, &s, 0) == pdTRUE);
            xSemaphoreGive(s_lock);
        }
        now = now_ms();
        if ((int32_t)(now - next_tick) >= 0) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            app_tick(s_app, now);
            xSemaphoreGive(s_lock);
            next_tick = now + 50;
        }
    }
}

uint32_t radio_dropped(void) { return s_dropped; }

void radio_start(app_t *app, SemaphoreHandle_t lock)
{
    s_app = app;
    s_lock = lock;
    s_q = xQueueCreate(128, sizeof(sample_t));

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    // EU channels 1-13. we only recieve, so tx power limits don't matter
    wifi_country_t country = { .cc = "SK", .schan = 1, .nchan = 13, .policy = WIFI_COUNTRY_POLICY_MANUAL };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filt));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(rx_cb));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

    xTaskCreatePinnedToCore(radio_task, "radio", 4096, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "listening, channels 1-13");
}
