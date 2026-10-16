/*
 * touch, three kinds:
 *  XPT2046 bit-banged on its own pins (the 2.8" CYD), or on the LCD's SPI bus (3.5" CYD).
 *    check pressure (Z1, Z2) first, a light press counts as nothing, otherwise the resistive
 *    panel invents taps. then 5 reads per axis, median.
 *  CST816 capacitive on I2C (round board). its axes may not match the screen, that's fine,
 *    the 5 cross alignment in ui.c sorts out swaps and mirrors for every kind
 *  GT911 capacitive on I2C (3.5" CYD, C version), asked for before the XPT2046 of the R version
 * boards without touch get tp_read() that never presses
 */
#include "touch.h"
#include "board.h"
#include "esp_log.h"

static const char *TAG = "touch";

#if TOUCH_GT911_TOO
/* GT911, capacitive, on the 3.5" CYD's C version (board.h). 16-bit register addresses, MSB
 * first. its address is 0x5D or 0x14, picked by the INT level at power on: both are asked, and
 * "911" at 0x8140 tells it is really one. it counts in screen pixels, portrait 320x480 */
#include "i2c_bus.h"
#include "esp_timer.h"

static i2c_master_dev_handle_t s_gt;
static int      s_gt_down, s_gt_x, s_gt_y;
static int64_t  s_gt_seen;

static int gt_reg(uint16_t reg, uint8_t *buf, int n)
{
    uint8_t r[2] = { (uint8_t)(reg >> 8), (uint8_t)reg };
    return i2c_master_transmit_receive(s_gt, r, 2, buf, (size_t)n, 20) == ESP_OK;
}

static int gt_find(void)
{
    const uint8_t addr[2] = { 0x5D, 0x14 };
    for (int i = 0; i < 2; i++) {
        if (!i2c_probe(addr[i])) continue;
        s_gt = i2c_dev(addr[i]);
        uint8_t id[4] = { 0 };
        if (gt_reg(0x8140, id, 4) && id[0] == '9' && id[1] == '1' && id[2] == '1') {
            ESP_LOGI(TAG, "GT911 at 0x%02x", addr[i]);
            return 1;
        }
        i2c_master_bus_rm_device(s_gt);              // something else answered, not ours
        s_gt = NULL;
    }
    return 0;
}

/* status 0x814E: bit 7 = fresh data, low bits = how many fingers. after reading it wants a 0 back.
 * while a finger is on it reports about every 10 ms, so a silence of 300 ms counts as lifted */
static int gt_read(int *rx, int *ry)
{
    uint8_t st = 0;
    int64_t now = esp_timer_get_time();
    if (gt_reg(0x814E, &st, 1) && (st & 0x80)) {
        uint8_t p[4];
        if ((st & 0x0F) && gt_reg(0x8150, p, 4)) {
            s_gt_x = p[0] | p[1] << 8;
            s_gt_y = p[2] | p[3] << 8;
            s_gt_down = 1;
        } else {
            s_gt_down = 0;
        }
        const uint8_t clr[3] = { 0x81, 0x4E, 0 };
        i2c_master_transmit(s_gt, clr, 3, 20);
        s_gt_seen = now;
    } else if (s_gt_down && now - s_gt_seen > 300000) {
        s_gt_down = 0;
    }
    if (s_gt_down) { *rx = s_gt_x; *ry = s_gt_y; }
    return s_gt_down;
}
#endif

#if TOUCH_XPT_BITBANG || TOUCH_XPT_SPI

#define Z_MIN 350            // below this its not a press

#if TOUCH_XPT_BITBANG
#include "driver/gpio.h"
#include "esp_rom_sys.h"

void tp_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_T_CLK) | (1ULL << PIN_T_MOSI) | (1ULL << PIN_T_CS),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_T_MISO) | (1ULL << PIN_T_IRQ),
        .mode = GPIO_MODE_INPUT,
    };
    gpio_config(&in);
    gpio_set_level(PIN_T_CS, 1);
    gpio_set_level(PIN_T_CLK, 0);
    ESP_LOGI(TAG, "XPT2046 on its own pins");
}

static void chip_select(int on) { gpio_set_level(PIN_T_CS, !on); }

// send command byte, then clock in 16 bits: busy bit, 12 data bits, 3 zeros
static int xfer(uint8_t cmd)
{
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(PIN_T_MOSI, (cmd >> i) & 1);
        esp_rom_delay_us(1);
        gpio_set_level(PIN_T_CLK, 1);
        esp_rom_delay_us(1);
        gpio_set_level(PIN_T_CLK, 0);
    }
    gpio_set_level(PIN_T_MOSI, 0);
    unsigned v = 0;
    for (int i = 0; i < 16; i++) {
        esp_rom_delay_us(1);
        gpio_set_level(PIN_T_CLK, 1);
        esp_rom_delay_us(1);
        v = (v << 1) | (unsigned)gpio_get_level(PIN_T_MISO);
        gpio_set_level(PIN_T_CLK, 0);
    }
    return (int)((v >> 3) & 0xFFF);
}

#else   /* XPT2046 as a second device on the LCD bus (bus is set up by disp_init) */
#include "driver/spi_master.h"

static spi_device_handle_t s_dev;

void tp_init(void)
{
#if TOUCH_GT911_TOO
    if (gt_find()) return;
    i2c_bus_free();                                  // nobody there: 33 is the XPT2046's chip select again
#endif
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 2 * 1000 * 1000,       // the XPT2046 wants it slow
        .mode = 0,
        .spics_io_num = PIN_T_CS,
        .queue_size = 1,
    };
    if (spi_bus_add_device(SPI2_HOST, &dev, &s_dev) != ESP_OK) ESP_LOGE(TAG, "XPT2046 not added to the bus");
}

static void chip_select(int on) { (void)on; }         // the SPI driver does CS for every transfer

static int xfer(uint8_t cmd)
{
    spi_transaction_t t = { .length = 24, .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA };
    t.tx_data[0] = cmd;
    if (!s_dev || spi_device_polling_transmit(s_dev, &t) != ESP_OK) return 0;
    return (int)(((t.rx_data[1] << 8) | t.rx_data[2]) >> 3) & 0xFFF;
}
#endif

static int median5(int *v)
{
    for (int i = 1; i < 5; i++)
        for (int j = i; j > 0 && v[j - 1] > v[j]; j--) { int t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    return v[2];
}

int tp_read(int *rx, int *ry)
{
#if TOUCH_GT911_TOO
    if (s_gt) return gt_read(rx, ry);
#endif
    chip_select(1);
    int z1 = xfer(0xB1), z2 = xfer(0xC1);
    int z = z1 + 4095 - z2;
    int pressed = z1 > 0 && z >= Z_MIN;
    if (pressed) {
        int xs[5], ys[5];
        for (int i = 0; i < 5; i++) xs[i] = xfer(0xD1);
        for (int i = 0; i < 5; i++) ys[i] = xfer(0x91);
        *rx = median5(xs);
        *ry = median5(ys);
        pressed = *rx > 50 && *rx < 4050 && *ry > 50 && *ry < 4050;   // values at the very edge are junk
    }
    xfer(0xD0);                                  // power down, pen interrupt on
    chip_select(0);
    return pressed;
}

#elif TOUCH_CST816
#include "i2c_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_master_dev_handle_t s_dev;

void tp_init(void)
{
    // the expander let the touch out of reset in disp_init, give it a moment to wake
    vTaskDelay(pdMS_TO_TICKS(50));
    if (!i2c_probe(CST816_ADDR)) {
        ESP_LOGW(TAG, "nothing at 0x%02x, going on without touch (BOOT button still works)", CST816_ADDR);
        return;
    }
    s_dev = i2c_dev(CST816_ADDR);
    // the CST816 dozes off after a few idle seconds and then ignores I2C untill touched. 0xFE = 1 keeps it awake
    i2c_master_transmit(s_dev, (const uint8_t[]){ 0xFE, 0x01 }, 2, 50);
    ESP_LOGI(TAG, "CST816 at 0x%02x", CST816_ADDR);
}

int tp_read(int *rx, int *ry)
{
    if (!s_dev) return 0;
    uint8_t reg = 0x02, b[5];                     // finger count, X high, X low, Y high, Y low
    if (i2c_master_transmit_receive(s_dev, &reg, 1, b, sizeof b, 20) != ESP_OK) return 0;
    if (!(b[0] & 0x0F)) return 0;
    *rx = ((b[1] & 0x0F) << 8) | b[2];
    *ry = ((b[3] & 0x0F) << 8) | b[4];
    return 1;
}

#else

void tp_init(void) { ESP_LOGI(TAG, "this board has no touch"); }
int tp_read(int *rx, int *ry) { (void)rx; (void)ry; return 0; }

#endif

int tp_raw_span(void)
{
#if TOUCH_GT911_TOO
    if (s_gt) return 480;                            // the GT911 counts screen pixels
#endif
    return TOUCH_RAW_SPAN;
}
