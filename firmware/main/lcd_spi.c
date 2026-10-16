/*
 * ILI9341, ST7796 and GC9A01 on esp_lcd SPI panel IO, no panel driver component, so nothing
 * gets downloaded at build. init values: ILI9341 from esp_lcd_ili9341 1.2.0, ST7796 from
 * esp_lcd_st7796 1.3.5 (both what ViewOwl uses on the same panels), GC9A01 from ViewOwl's
 * lcd_gc9a01.c. frame = strips: first one RAMWR (0x2C) for the full window, the rest
 * RAMWRC (0x3C), all three chips know it
 */
#include "display.h"
#if DISP_LCD_SPI

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_attr.h"
#if LCD_CHIP_GC9A01
#include "i2c_bus.h"
#endif

static const char *TAG = "lcd";

static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_buf[2];                      // fill one while DMA sends the other
static int s_k;                                 // which one is next

/* careful: dont send a strip while the previous one is still in flight.
 * that sometimes hung for ever, core dump showed the screen task stuck in
 * spi_device_acquire_bus (light views draw faster than the 40 MHz bus).
 * so wait for the done callback first, ViewOwl does the same. */
static SemaphoreHandle_t s_done;
static volatile uint32_t s_lost;

static bool IRAM_ATTR on_strip_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *e, void *ctx)
{
    (void)io; (void)e; (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

uint32_t disp_lost(void) { return s_lost; }
uint32_t disp_frame_ms(void) { return 0; }
void disp_frame_end(void) { }
void disp_full_next(void) { }

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[15];
    uint8_t wait_ms;
} init_cmd_t;

#if LCD_CHIP_ILI9341
static const init_cmd_t INIT[] = {
    { 0x01, 0, { 0 }, 120 },                                  /* software reset */
    { 0x11, 0, { 0 }, 120 },                                  /* sleep out */
    { 0xCF, 3, { 0x00, 0xAA, 0xE0 }, 0 },                    /* power control B */
    { 0xED, 4, { 0x67, 0x03, 0x12, 0x81 }, 0 },              /* power on sequence */
    { 0xE8, 3, { 0x8A, 0x01, 0x78 }, 0 },                    /* driver timing A */
    { 0xCB, 5, { 0x39, 0x2C, 0x00, 0x34, 0x02 }, 0 },        /* power control A */
    { 0xF7, 1, { 0x20 }, 0 },                                /* pump ratio */
    { 0xEA, 2, { 0x00, 0x00 }, 0 },                          /* driver timing B */
    { 0xC0, 1, { 0x23 }, 0 },                                /* power control 1 */
    { 0xC1, 1, { 0x11 }, 0 },                                /* power control 2 */
    { 0xC5, 2, { 0x43, 0x4C }, 0 },                          /* VCOM 1 */
    { 0xC7, 1, { 0xA0 }, 0 },                                /* VCOM 2 */
    { 0xB1, 2, { 0x00, 0x1B }, 0 },                          /* frame rate 70 Hz */
    { 0xF2, 1, { 0x00 }, 0 },                                /* 3-gamma off */
    { 0x26, 1, { 0x01 }, 0 },                                /* gamma curve 1 */
    { 0xE0, 15, { 0x1F, 0x36, 0x36, 0x3A, 0x0C, 0x05, 0x4F, 0x87, 0x3C, 0x08, 0x11, 0x35, 0x19, 0x13, 0x00 }, 0 },
    { 0xE1, 15, { 0x00, 0x09, 0x09, 0x05, 0x13, 0x0A, 0x30, 0x78, 0x43, 0x07, 0x0E, 0x0A, 0x26, 0x2C, 0x1F }, 0 },
    { 0xB7, 1, { 0x07 }, 0 },                                /* entry mode */
    { 0xB6, 3, { 0x08, 0x82, 0x27 }, 0 },                    /* display function */
    { 0x36, 1, { LCD_MADCTL }, 0 },                          /* landscape, BGR */
    { 0x3A, 1, { 0x55 }, 0 },                                /* 16 bits a pixel */
    { 0x20, 0, { 0 }, 0 },                                   /* no inversion */
    { 0x29, 0, { 0 }, 20 },                                  /* display on */
};
#define CHIP "ILI9341"

#if LCD_PROFILES
/* the other two CYD revisions (board.h). values from rzeldent/platformio-espressif32-sunton,
 * the orientation turned into our landscape. a software reset first, so one can follow another */
static const init_cmd_t INIT_V2[] = {                         /* USB-C: ILI9341, its own power and gamma */
    { 0x01, 0, { 0 }, 120 },
    { 0xCF, 3, { 0x00, 0xC1, 0x30 }, 0 },
    { 0xED, 4, { 0x64, 0x03, 0x12, 0x81 }, 0 },
    { 0xE8, 3, { 0x85, 0x00, 0x78 }, 0 },
    { 0xCB, 5, { 0x39, 0x2C, 0x00, 0x34, 0x02 }, 0 },
    { 0xF7, 1, { 0x20 }, 0 },
    { 0xEA, 2, { 0x00, 0x00 }, 0 },
    { 0xC0, 1, { 0x10 }, 0 },
    { 0xC1, 1, { 0x00 }, 0 },
    { 0xC5, 2, { 0x30, 0x30 }, 0 },
    { 0xC7, 1, { 0xB7 }, 0 },
    { 0x3A, 1, { 0x55 }, 0 },
    { 0x36, 1, { LCD_MADCTL }, 0 },                          /* same way round as v1 */
    { 0xB1, 2, { 0x00, 0x1A }, 0 },
    { 0xB6, 3, { 0x08, 0x82, 0x27 }, 0 },
    { 0xF2, 1, { 0x00 }, 0 },
    { 0x26, 1, { 0x01 }, 0 },
    { 0xE0, 15, { 0x0F, 0x2A, 0x28, 0x08, 0x0E, 0x08, 0x54, 0xA9, 0x43, 0x0A, 0x0F, 0x00, 0x00, 0x00, 0x00 }, 0 },
    { 0xE1, 15, { 0x00, 0x15, 0x17, 0x07, 0x11, 0x06, 0x2B, 0x56, 0x3C, 0x05, 0x10, 0x0F, 0x3F, 0x3F, 0x0F }, 0 },
    { 0x21, 0, { 0 }, 0 },                                   /* inversion on: this panel needs it */
    { 0x11, 0, { 0 }, 120 },
    { 0x29, 0, { 0 }, 20 },
};
/* v3: an ST7789 on the chip's own settings (like Espressif's esp_lcd driver). against v1 it is RGB
 * not BGR and mirrored on one axis, so MADCTL loses BGR and MX: 0xE8 -> 0xA0. with the v1 start-up
 * a viewer's v3 showed light blue on white and upside down, which is exactly those three */
static const init_cmd_t INIT_V3[] = {
    { 0x01, 0, { 0 }, 150 },                                  /* software reset */
    { 0x11, 0, { 0 }, 120 },                                  /* sleep out */
    { 0x3A, 1, { 0x55 }, 10 },                               /* 16 bits a pixel */
    { 0x36, 1, { 0xA0 }, 0 },                                /* MY|MV, RGB */
    { 0x21, 0, { 0 }, 0 },                                   /* inversion on */
    { 0x13, 0, { 0 }, 10 },                                  /* normal display mode */
    { 0x29, 0, { 0 }, 20 },
};
#endif

#elif LCD_CHIP_ST7796
static const init_cmd_t INIT[] = {
    { 0x01, 0, { 0 }, 120 },                                  /* software reset (no RST pin) */
    { 0x11, 0, { 0 }, 100 },                                  /* sleep out */
    { 0x36, 1, { LCD_MADCTL }, 0 },
    { 0x3A, 1, { 0x55 }, 0 },                                /* 16 bits a pixel */
    { 0xF0, 1, { 0xC3 }, 0 },                                /* command set unlock, part 1 */
    { 0xF0, 1, { 0x96 }, 0 },                                /* and part 2 */
    { 0xB4, 1, { 0x01 }, 0 },
    { 0xB7, 1, { 0xC6 }, 0 },
    { 0xE8, 8, { 0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33 }, 0 },
    { 0xC1, 1, { 0x06 }, 0 },
    { 0xC2, 1, { 0xA7 }, 0 },
    { 0xC5, 1, { 0x18 }, 0 },
    { 0xE0, 14, { 0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B }, 0 },
    { 0xE1, 14, { 0xF0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2D, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B }, 0 },
    { 0xF0, 1, { 0x3C }, 0 },                                /* lock again */
    { 0xF0, 1, { 0x69 }, 0 },
    { 0x20, 0, { 0 }, 0 },                                   /* no inversion, like ViewOwl */
    { 0x29, 0, { 0 }, 20 },
};
#define CHIP "ST7796"

#elif LCD_CHIP_GC9A01
static const init_cmd_t INIT[] = {
    { 0xEF, 0, { 0 }, 0 },
    { 0xEB, 1, { 0x14 }, 0 },
    { 0xFE, 0, { 0 }, 0 },
    { 0xEF, 0, { 0 }, 0 },
    { 0xEB, 1, { 0x14 }, 0 },
    { 0x84, 1, { 0x40 }, 0 },
    { 0x85, 1, { 0xFF }, 0 },
    { 0x86, 1, { 0xFF }, 0 },
    { 0x87, 1, { 0xFF }, 0 },
    { 0x88, 1, { 0x0A }, 0 },
    { 0x89, 1, { 0x21 }, 0 },
    { 0x8A, 1, { 0x00 }, 0 },
    { 0x8B, 1, { 0x80 }, 0 },
    { 0x8C, 1, { 0x01 }, 0 },
    { 0x8D, 1, { 0x01 }, 0 },
    { 0x8E, 1, { 0xFF }, 0 },
    { 0x8F, 1, { 0xFF }, 0 },
    { 0xB6, 2, { 0x00, 0x20 }, 0 },
    { 0x36, 1, { LCD_MADCTL }, 0 },
    { 0x3A, 1, { 0x05 }, 0 },
    { 0x90, 4, { 0x08, 0x08, 0x08, 0x08 }, 0 },
    { 0xBD, 1, { 0x06 }, 0 },
    { 0xBC, 1, { 0x00 }, 0 },
    { 0xFF, 3, { 0x60, 0x01, 0x04 }, 0 },
    { 0xC3, 1, { 0x13 }, 0 },
    { 0xC4, 1, { 0x13 }, 0 },
    { 0xC9, 1, { 0x22 }, 0 },
    { 0xBE, 1, { 0x11 }, 0 },
    { 0xE1, 2, { 0x10, 0x0E }, 0 },
    { 0xDF, 3, { 0x21, 0x0C, 0x02 }, 0 },
    { 0xF0, 6, { 0x45, 0x09, 0x08, 0x08, 0x26, 0x2A }, 0 },
    { 0xF1, 6, { 0x43, 0x70, 0x72, 0x36, 0x37, 0x6F }, 0 },
    { 0xF2, 6, { 0x45, 0x09, 0x08, 0x08, 0x26, 0x2A }, 0 },
    { 0xF3, 6, { 0x43, 0x70, 0x72, 0x36, 0x37, 0x6F }, 0 },
    { 0xED, 2, { 0x1B, 0x0B }, 0 },
    { 0xAE, 1, { 0x77 }, 0 },
    { 0xCD, 1, { 0x63 }, 0 },
    { 0x70, 9, { 0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03 }, 0 },
    { 0xE8, 1, { 0x34 }, 0 },
    { 0x62, 12, { 0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70 }, 0 },
    { 0x63, 12, { 0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70 }, 0 },
    { 0x64, 7, { 0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07 }, 0 },
    { 0x66, 10, { 0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00 }, 0 },
    { 0x67, 10, { 0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98 }, 0 },
    { 0x74, 7, { 0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00 }, 0 },
    { 0x98, 2, { 0x3E, 0x07 }, 0 },
    { 0x35, 0, { 0 }, 0 },                                   /* tearing line on (pin not used) */
    { 0x21, 0, { 0 }, 0 },                                   /* inversion ON, this panel needs it */
    { 0x11, 0, { 0 }, 120 },
    { 0x29, 0, { 0 }, 20 },
};
#define CHIP "GC9A01"

/* PI4IOE5V6408 expander: backlight on, LCD and touch out of reset */
static void expander_power_on(void)
{
    i2c_master_dev_handle_t ex = i2c_dev(IOEX_ADDR);
    const uint8_t mask = IOEX_BIT_BL | IOEX_BIT_RST | IOEX_BIT_TRST;
    const uint8_t seq[][2] = {
        { 0x03, mask },                      /* direction: these are outputs */
        { 0x07, (uint8_t)~mask },            /* hi-z off for them */
        { 0x0B, 0x00 },                      /* no pulls */
        { 0x05, IOEX_BIT_BL },               /* resets low, backlight on */
    };
    for (size_t i = 0; i < sizeof seq / sizeof seq[0]; i++)
        if (i2c_master_transmit(ex, seq[i], 2, 100) != ESP_OK) ESP_LOGE(TAG, "expander write %02x failed", seq[i][0]);
    vTaskDelay(pdMS_TO_TICKS(20));
    i2c_master_transmit(ex, (const uint8_t[]){ 0x05, mask }, 2, 100);   /* resets released */
    vTaskDelay(pdMS_TO_TICKS(120));
}
#endif

static void cmd(uint8_t c, const uint8_t *d, size_t n)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, c, d, n));
}

void disp_backlight(int percent)
{
    if (PIN_LCD_BL < 0) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)(1023 * percent / 100));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* screen profiles: one start-up for most boards, three for the CYD revisions (board.h) */
typedef struct { const char *name; const init_cmd_t *cmds; size_t n; } profile_t;
#define PROFILE(name, t) { name, t, sizeof t / sizeof t[0] }
static const profile_t PROFILE_LIST[] = {
#if LCD_PROFILES
    PROFILE("ILI9341 V1", INIT),
    PROFILE("ILI9341 V2", INIT_V2),
    PROFILE("ST7789 V3", INIT_V3),
#else
    PROFILE(CHIP, INIT),
#endif
};
#define NPROFILES ((int)(sizeof PROFILE_LIST / sizeof PROFILE_LIST[0]))
static int s_profile, s_ready;

static void run_profile(void)
{
    const profile_t *p = &PROFILE_LIST[s_profile];
    for (size_t i = 0; i < p->n; i++) {
        cmd(p->cmds[i].cmd, p->cmds[i].len ? p->cmds[i].data : NULL, p->cmds[i].len);
        if (p->cmds[i].wait_ms) vTaskDelay(pdMS_TO_TICKS(p->cmds[i].wait_ms));
    }
}

int disp_profiles(void) { return NPROFILES; }
const char *disp_profile_name(void) { return PROFILE_LIST[s_profile].name; }

/* before disp_init: just remember it. after: start the screen again with it, between frames
 * (the strip still in flight is waited for first) */
void disp_profile(int p)
{
    s_profile = p >= 0 && p < NPROFILES ? p : 0;
    if (!s_ready) return;
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(200)) != pdTRUE) s_lost++;
    run_profile();
    xSemaphoreGive(s_done);
    ESP_LOGI(TAG, "screen started as %s", PROFILE_LIST[s_profile].name);
}

void disp_init(void)
{
    if (PIN_LCD_BL >= 0) {
        ledc_timer_config_t tcfg = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ESP_ERROR_CHECK(ledc_timer_config(&tcfg));
        ledc_channel_config_t ccfg = {
            .gpio_num = PIN_LCD_BL,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = LEDC_CHANNEL_0,
            .timer_sel = LEDC_TIMER_0,
            .duty = 0,
            .hpoint = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ccfg));
    }
#if LCD_CHIP_GC9A01
    expander_power_on();
#endif

    const size_t strip_bytes = GFX_W * DISP_STRIP * sizeof(uint16_t);
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)strip_bytes + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io = {
        .dc_gpio_num = PIN_LCD_DC,
        .cs_gpio_num = PIN_LCD_CS,
        .pclk_hz = LCD_SPI_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_strip_done,
    };
    s_done = xSemaphoreCreateBinary();
    xSemaphoreGive(s_done);                        /* nothing in flight yet */
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io, &s_io));

    for (int k = 0; k < 2; k++) {
        s_buf[k] = heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_buf[k]) ESP_LOGE(TAG, "no DMA memory for strip %d", k);
    }

    run_profile();
    s_ready = 1;
    ESP_LOGI(TAG, "%s %dx%d, SPI %d MHz, %d-row strips", PROFILE_LIST[s_profile].name, GFX_W, GFX_H,
             LCD_SPI_HZ / 1000000, DISP_STRIP);
}

void disp_strip(const canvas_t *cv)
{
    uint16_t *buf = s_buf[s_k];
    s_k ^= 1;
    gfx_pack565(cv, buf);   // this buffer went out two strips ago and we waited for that, so its free
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(200)) != pdTRUE) s_lost++;   // max 200 ms, then count it lost and go on
    const size_t bytes = GFX_W * cv->h * sizeof(uint16_t);
    if (cv->y0 == 0) {
        cmd(0x2A, (const uint8_t[]){ 0, 0, (GFX_W - 1) >> 8, (GFX_W - 1) & 255 }, 4);
        cmd(0x2B, (const uint8_t[]){ 0, 0, (GFX_H - 1) >> 8, (GFX_H - 1) & 255 }, 4);
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, 0x2C, buf, bytes));
    } else {
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, 0x3C, buf, bytes));
    }
}

#endif
