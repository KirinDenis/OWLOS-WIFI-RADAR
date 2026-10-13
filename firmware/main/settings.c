#include "settings.h"
#include "nvs.h"

#define NS "radarscan"     // nvs namespace

int settings_load_cal(touch_cal_t *cal)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;
    size_t len = sizeof *cal;
    esp_err_t e = nvs_get_blob(h, "cal", cal, &len);
    nvs_close(h);
    return e == ESP_OK && len == sizeof *cal;
}

void settings_save_cal(const touch_cal_t *cal)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "cal", cal, sizeof *cal);
    nvs_commit(h);
    nvs_close(h);
}

int settings_load_theme(void)
{
    nvs_handle_t h;
    uint8_t t = 0;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;
    nvs_get_u8(h, "theme", &t);
    nvs_close(h);
    return t;
}

void settings_save_theme(int theme)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "theme", (uint8_t)theme);
    nvs_commit(h);
    nvs_close(h);
}
