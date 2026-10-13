/*
 * OWLOS WiFi Radar - passive 2.4 GHz wifi radar on the ViewOwl screens (core/config.h).
 * core 0: wifi stack, radio task (channel hopping), mapper (once a second, see core/map.h)
 * core 1: screen task - touch, keys and BOOT button in, copy radio state, draw the frame
 *         in strips, the display driver sends one while we draw the next.
 *         the ESP32-C3 has one core, everything shares it there
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "button.h"
#include "gfx.h"
#include "display.h"
#include "keys.h"
#include "map.h"
#include "radio.h"
#include "settings.h"
#include "flashstore.h"
#include "store.h"
#include "touch.h"
#include "ui.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "radarscan";

static app_t             s_app;
static ui_t              s_ui;
static map_t             s_map;
static SemaphoreHandle_t s_lock;
static volatile int      s_map_reset;
static saved_net_t       s_nets[MAX_CONTACTS];

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static uint32_t clock_us(void) { return (uint32_t)esp_timer_get_time(); }

// take the shared lock, but if it takes more than 2 s print who holds it (then keep waiting)
static void lock_app(const char *who)
{
    while (xSemaphoreTake(s_lock, pdMS_TO_TICKS(2000)) != pdTRUE) {
        TaskHandle_t h = xSemaphoreGetMutexHolder(s_lock);
        ESP_LOGW(TAG, "%s waits for the lock, held by %s", who, h ? pcTaskGetName(h) : "?");
    }
}

// dump what we see to serial, called every 10 s
static void log_contacts(const app_t *a)
{
    const mapview_t *m = &a->map;
    ESP_LOGI(TAG, "%d networks, %u frames/s, ch %d, sweep %d | map %s T=%d n=%d spread %.1f m %s",
             a->n, a->pkt_rate, a->channel, a->sweep, m->active ? "on" : "off", m->T, m->n,
             (double)m->spread, m->aligned ? "aligned" : "L/R?");
    for (int i = 0; i < a->n; i++) {
        const contact_t *c = &a->c[i];
        char brg[24] = "---";
        if (c->has_bearing) snprintf(brg, sizeof brg, "%03d+-%d", (int)c->bearing, (int)c->bearing_sd);
        ESP_LOGI(TAG, "  %02x:%02x:%02x:%02x:%02x:%02x ch%2d sec%d %4d dBm brg %-8s '%s'",
                 c->bssid[0], c->bssid[1], c->bssid[2], c->bssid[3], c->bssid[4], c->bssid[5],
                 c->channel, c->sec, (int)c->rssi, brg, c->hidden ? "[hidden]" : c->ssid);
    }
}

/* once a second: snapshot all levels, then run the solver.
 * solver works on its own copy, no lock held while it runs */
static void mapper_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    uint32_t last_brg = 0;
    int save_in = 180;                                // seconds untill next flash save
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
        if (s_map_reset) {
            map_reset(&s_map);
            s_map_reset = 0;
            save_in = 5;                              // new walk, overwrite the saved one soon
        }
        lock_app("mapper");
        map_snapshot(&s_map, &s_app);
        xSemaphoreGive(s_lock);

        map_solve(&s_map, 4);

        lock_app("mapper");
        if (s_app.bearing_ms != last_brg) {
            last_brg = s_app.bearing_ms;
            float r = map_align_turn(&s_map, &s_app);
            ESP_LOGI(TAG, "turn vs walk map: agreement %.2f%s", (double)r, s_map.aligned ? ", aligned" : "");
            save_in = 5;
        }
        map_publish(&s_map, &s_app);
        xSemaphoreGive(s_lock);

        if (--save_in <= 0) {
            save_in = 180;
            lock_app("mapper");
            int n = store_collect(&s_app, s_nets);    // small copy under lock, the slow flash write without it
            xSemaphoreGive(s_lock);
            int64_t t0 = esp_timer_get_time();
            int ok = fstore_save(&s_map, s_nets, n);
            ESP_LOGI(TAG, "saved to flash: %s, %d networks, %d walk seconds, %d ms", ok ? "ok" : "FAILED", n,
                     s_map.T, (int)((esp_timer_get_time() - t0) / 1000));
        }
    }
}

/* touch is sampled between the strips too, not once a frame. at 8 fps (round C3, 3.5")
 * a swipe gave 1-2 points and came out as a tap, so only radar <-> target ever worked.
 * the samples queue up and go to ui_touch at the start of the next frame, in order */
typedef struct { uint8_t down; int16_t x, y, rx, ry; uint32_t t; } tsample_t;
static tsample_t s_ts[24];
static int       s_nts;
static uint32_t  s_ts_last;

static void touch_sample(int force)
{
    uint32_t now = now_ms();
    if (s_nts >= (int)(sizeof s_ts / sizeof s_ts[0]) || (!force && now - s_ts_last < 15u)) return;
    int rx = 0, ry = 0, x = 0, y = 0;
    int down = tp_read(&rx, &ry);
    if (down) touch_apply(&s_ui.cal, rx, ry, &x, &y);
    s_ts[s_nts++] = (tsample_t){ (uint8_t)down, (int16_t)x, (int16_t)y, (int16_t)rx, (int16_t)ry, now };
    s_ts_last = now;
}

static void ui_task(void *arg)
{
    (void)arg;
    disp_init();                                      // init here so the SPI interrupt is on this core, away from wifi
    uint32_t *work = heap_caps_malloc(GFX_W * DISP_STRIP * sizeof(uint32_t), MALLOC_CAP_INTERNAL);
    if (!work || !ui_init(&s_ui, now_ms())) {
        ESP_LOGE(TAG, "not enough memory for the screen (%s), free heap %u, largest block %u", work ? "radar tables" : "strip buffer",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        vTaskDelete(NULL);
    }
    button_init();
    keys_init();
    // watchdog on this task too: stuck 10 s = reboot + core dump that shows where
    esp_task_wdt_add(NULL);
    tp_init();
    ui_set_theme(&s_ui, settings_load_theme());
#if RADAR_TOUCH_CAL
    /* no saved alignment or a crazy one -> align again. the scale has to be near
     * screen px / raw units (320 px over ~3700 raw on the CYD is ~0.09), a third or three
     * times that is garbage (the old 3-point version once saved 0.7 from wobbly presses) */
    int have = settings_load_cal(&s_ui.cal);
    const float want = (float)GFX_W / TOUCH_RAW_SPAN;
    float sx = fabsf(s_ui.cal.a) + fabsf(s_ui.cal.b), sy = fabsf(s_ui.cal.d) + fabsf(s_ui.cal.e);
    if (!have || sx < want / 3 || sx > want * 3 || sy < want / 3 || sy > want * 3) {
        if (have) ESP_LOGW(TAG, "stored touch alignment is implausible: aligning again");
        ui_start_calibration(&s_ui);
    }
    ESP_LOGI(TAG, "touch alignment x = %.4f rx + %.4f ry + %.1f, y = %.4f rx + %.4f ry + %.1f",
                  (double)s_ui.cal.a, (double)s_ui.cal.b, (double)s_ui.cal.c,
                  (double)s_ui.cal.d, (double)s_ui.cal.e, (double)s_ui.cal.f);
#endif
    ESP_LOGI(TAG, "%s, %dx%d, free heap %u, largest block %u", RADAR_NAME, GFX_W, GFX_H,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    uint32_t frames = 0, t_stat = now_ms(), t_log = now_ms(), busy_us = 0, last_frame = 0;
    ui_clock_us = clock_us;
    int lit = 0, was_down = 0, drawn = 0, last_view = -1;
    for (;;) {
        uint32_t now = now_ms();
        ui_cmd_t cmd;
        memset(&cmd, 0, sizeof cmd);
        int input = 0;

        int ev = button_poll(now);
        if (ev) { ui_button(&s_ui, ev == BTN_LONG ? 2 : 1, &cmd); input = 1; }
        int key = keys_poll(now);
        if (key) { ui_key(&s_ui, key, &cmd); input = 1; }
        touch_sample(1);
        int down = 0, rx = 0, ry = 0, x = 0, y = 0;
        for (int i = 0; i < s_nts; i++) {
            const tsample_t *s = &s_ts[i];
            ui_touch(&s_ui, s->down, s->x, s->y, s->rx, s->ry, s->t, &cmd);
            down = s->down; x = s->x; y = s->y; rx = s->rx; ry = s->ry;
        }
        s_nts = 0;

        if (cmd.map_reset) s_map_reset = 1;
        if (cmd.save_cal) {
            settings_save_cal(&s_ui.cal);
            ESP_LOGI(TAG, "touch aligned, worst cross %.1f px", (double)s_ui.cal_err);
        }
        if (cmd.save_theme) settings_save_theme(s_ui.theme);

        lock_app("ui");
        if (cmd.select) app_select(&s_app, cmd.select_id);
        if (cmd.sweep) app_sweep_toggle(&s_app, now);   // TODO: compass module, so the turn doesn't depend on how steady you turn
        memcpy(&s_ui.snap, &s_app, sizeof s_app);
        xSemaphoreGive(s_lock);

        /* e-paper: a new picture only every disp_frame_ms(), or right after a key.
         * LCDs draw as fast as they can */
        uint32_t every = disp_frame_ms();
        if (every && drawn && !input && now - last_frame < every) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        last_frame = now;
        drawn = 1;

        int64_t t0 = esp_timer_get_time();
        ui_frame(&s_ui, now);
        if (s_ui.view != last_view) {                 // e-paper: a new screen gets a full refresh, not drawn on top
            disp_full_next();
            last_view = s_ui.view;
        }
        for (int sy = 0; sy < GFX_H; sy += DISP_STRIP) {
            canvas_t cv = { work, sy, sy + DISP_STRIP <= GFX_H ? DISP_STRIP : GFX_H - sy };   // last one may be short
            ui_strip(&s_ui, &cv);
            disp_strip(&cv);
            touch_sample(0);                          // every 15 ms or so, see touch_sample()
        }
        disp_frame_end();
        busy_us += (uint32_t)(esp_timer_get_time() - t0);
        frames++;

        if (lit < 100) disp_backlight(lit += 10);     // fade backlight in once theres a picture

        if (now - t_stat >= 5000u) {
            uint32_t f = frames ? frames : 1;
            ESP_LOGI(TAG, "%u fps, %u ms a frame, view %d, heap %u, dropped %u, lost %u",
                     (unsigned)(frames * 1000u / (now - t_stat)), (unsigned)(busy_us / 1000u / f),
                     s_ui.view, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)radio_dropped(), (unsigned)disp_lost());
            ESP_LOGI(TAG, "us a frame: bg %u disc %u radar %u decor %u panel %u toast %u",
                     (unsigned)(s_ui.prof[0] / f), (unsigned)(s_ui.prof[1] / f), (unsigned)(s_ui.prof[5] / f),
                     (unsigned)(s_ui.prof[2] / f), (unsigned)(s_ui.prof[3] / f), (unsigned)(s_ui.prof[4] / f));
            memset(s_ui.prof, 0, sizeof s_ui.prof);
            frames = 0;
            busy_us = 0;
            t_stat = now;
        }
        if (now - t_log >= 10000u) {
            log_contacts(&s_ui.snap);
            t_log = now;
        }
        if (down != was_down) {                        // log every press raw + mapped, handy to check calibration
            if (down) ESP_LOGI(TAG, "touch at raw %d %d -> screen %d %d, view %d", rx, ry, x, y, s_ui.view);
            else ESP_LOGI(TAG, "touch lifted at screen %d %d", s_ui.t_x, s_ui.t_y);
            was_down = down;
        }
        if (key) ESP_LOGI(TAG, "key %d, view %d", key, s_ui.view);
        esp_task_wdt_reset();
        vTaskDelay(1);                                // let the idle task run, the watchdog checks it
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();                 // wifi keeps its calibration in NVS, we keep touch cal + theme there
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_lock = xSemaphoreCreateMutex();
    app_init(&s_app, now_ms());
    map_reset(&s_map);
    if (fstore_init()) {                              // continue from last session if there is a save
        int n = 0;
        if (fstore_load(&s_map, s_nets, &n)) {
            store_restore(&s_app, s_nets, n, now_ms());
            ESP_LOGI(TAG, "restored %d networks and a %d-snapshot walk map", n, s_map.T);
        }
    }
    radio_start(&s_app, s_lock);
    xTaskCreatePinnedToCore(mapper_task, "mapper", 6144, NULL, 2, NULL, 0);
    // one core (C3): the screen never stops drawing, so it goes under the mapper or that one starves
    if (portNUM_PROCESSORS > 1) xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 5, NULL, 1);
    else xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, 1, NULL, 0);
}
