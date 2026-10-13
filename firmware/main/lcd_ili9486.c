/*
 * ILI9486 on Waveshare's RPi LCD (A). the board turns SPI into the panel's 16 bit bus with
 * shift registers, so every command and parameter byte goes out as a 16 bit word (real byte
 * first, then 0x00) with its own CS pulse. pixels are 16 bit anyway and go out in one burst.
 * esp_lcd with 8 bit commands leaves this panel grey (ViewOwl found that the hard way),
 * so this is plain spi_master, same sequence as ViewOwl's esp_lcd_ili9486.c / TFT_eSPI.
 * no DMA ping-pong here, 20 MHz is the limit and a strip just goes out while we wait
 */
#include "display.h"
#if DISP_ILI9486

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "lcd";
static spi_device_handle_t s_spi;
static uint16_t *s_buf;

uint32_t disp_lost(void) { return 0; }
uint32_t disp_frame_ms(void) { return 0; }
void disp_frame_end(void) { }
void disp_full_next(void) { }
void disp_backlight(int percent) { (void)percent; }    // wired to power on this board

/* one padded byte. careful: with SPI_TRANS_USE_TXDATA tx_data[1] goes out FIRST
 * (it lands in W0[15:8]), so the real byte goes in [1] */
static void tx16(uint8_t b)
{
    spi_transaction_t t = { .length = 16, .flags = SPI_TRANS_USE_TXDATA };
    t.tx_data[0] = 0x00;
    t.tx_data[1] = b;
    gpio_set_level(PIN_LCD_CS, 0);
    spi_device_polling_transmit(s_spi, &t);
    gpio_set_level(PIN_LCD_CS, 1);
}

static void cmd(uint8_t c, const uint8_t *d, int n)
{
    gpio_set_level(PIN_LCD_DC, 0);
    tx16(c);
    gpio_set_level(PIN_LCD_DC, 1);
    for (int i = 0; i < n; i++) tx16(d[i]);
}

static void window(int x0, int y0, int x1, int y1)      // inclusive
{
    cmd(0x2A, (const uint8_t[]){ x0 >> 8, x0 & 255, x1 >> 8, x1 & 255 }, 4);
    cmd(0x2B, (const uint8_t[]){ y0 >> 8, y0 & 255, y1 >> 8, y1 & 255 }, 4);
}

void disp_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_LCD_CS) | (1ULL << PIN_LCD_DC) | (1ULL << PIN_LCD_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_set_level(PIN_LCD_CS, 1);
    gpio_set_level(PIN_LCD_DC, 1);

    const size_t strip_bytes = GFX_W * DISP_STRIP * sizeof(uint16_t);
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_LCD_SCLK,
        .mosi_io_num = PIN_LCD_MOSI,
        .miso_io_num = PIN_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)strip_bytes,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = {
        .clock_speed_hz = LCD_SPI_HZ,
        .mode = 0,
        .spics_io_num = -1,                       // CS by hand, see tx16()
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_spi));
    s_buf = heap_caps_malloc(strip_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_buf) ESP_LOGE(TAG, "no DMA memory for the strip");

    gpio_set_level(PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    cmd(0x11, NULL, 0);                                   // sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0x3A, (const uint8_t[]){ 0x55 }, 1);              // 16 bits a pixel, 0x66 gives wrong colours on these
    cmd(0xC2, (const uint8_t[]){ 0x44 }, 1);              // power control 3
    cmd(0xC5, (const uint8_t[]){ 0x00, 0x00, 0x00, 0x00 }, 4);   // VCOM
    cmd(0xE0, (const uint8_t[]){ 0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98, 0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x00 }, 15);
    cmd(0xE1, (const uint8_t[]){ 0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75, 0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00 }, 15);
    cmd(0x20, NULL, 0);                                   // inversion off, the (A) board wants it off
    cmd(0x36, (const uint8_t[]){ LCD_MADCTL }, 1);
    cmd(0x29, NULL, 0);                                   // display on
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_LOGI(TAG, "ILI9486 %dx%d, SPI %d MHz, %d-row strips", GFX_W, GFX_H, LCD_SPI_HZ / 1000000, DISP_STRIP);
}

void disp_strip(const canvas_t *cv)
{
    gfx_pack565(cv, s_buf);
    window(0, cv->y0, GFX_W - 1, cv->y0 + cv->h - 1);
    gpio_set_level(PIN_LCD_DC, 0);
    tx16(0x2C);                                           // memory write
    gpio_set_level(PIN_LCD_DC, 1);
    spi_transaction_t t = { .length = (size_t)GFX_W * cv->h * 16, .tx_buffer = s_buf };
    gpio_set_level(PIN_LCD_CS, 0);                        // the whole strip in one CS low
    spi_device_transmit(s_spi, &t);
    gpio_set_level(PIN_LCD_CS, 1);
}

#endif
