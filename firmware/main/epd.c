/*
 * e-paper, SSD1683, bit-banged SPI, 4 greys. the strips are turned into 2 bit codes
 * (core/paper.c) and collected into a frame, disp_frame_end() puts it on the glass.
 * a full 4-grey refresh (~3 s, it flashes) only for a new screen and every 10 min.
 * in between the changes are drawn on top with a partial refresh: black and white only,
 * no flash, and pixels that didn't change keep their grey. if nothing changed the glass
 * isn't touched at all. that is why ui_paper.c keeps grey to the parts that never change.
 *
 * 4.2" 400x300: one controller. 5.79" 792x272: two controllers in a cascade, master and
 * slave, one CS, the slave answers to the master's opcodes + 0x80.
 * the init, the grey LUTs and the plane packing are ViewOwl's epd_4in2.c / epd_wide.c,
 * which show 4 clean greys on these panels.
 * codes: 3 white, 2 light grey, 1 dark grey, 0 black. RAM 0x24 gets bit 0 of the code,
 * RAM 0x26 bit 1 (white and dark grey -> 1 in 0x24, white and light grey -> 1 in 0x26)
 */
#include "display.h"
#if DISP_EPD

#include <string.h>
#include "paper.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd";

#define SPI_DELAY_US    1         // half clock, the panel is happy well under 1 MHz
#define BUSY_TIMEOUT_MS 10000

#define FULL_EVERY_US  (10LL * 60 * 1000000)  // greys repainted and ghosts cleared every 10 min
#define FULL_GAP_US    (3LL * 60 * 1000000)   // but not more often than this for wrong greys
/* this many pixels with a wrong grey -> full refresh. the soft edges of changing digits
 * alone make 2000-4000 a minute (measured on the 5.79"), and those don't matter, they just
 * turn crisp. the grey fills do, they are thousands of pixels */
#define GREY_DEBT_MAX  8000

/* two pictures of 2 bit codes (4 px a byte, left pixel on top): the new one, and a model
 * of what really is on the glass. a partial refresh can only put black or white, so the
 * model knows where greys are missing (the "grey debt"). 54 KB each on the wide panel,
 * they go to PSRAM when the board has it */
static uint8_t *s_frame, *s_shown;
#define FRAME_BYTES (PAPER_STRIDE * GFX_H)
static uint32_t s_lost;
static int      s_full = 1;                       // next refresh is a full grey one
static int64_t  s_last_full;

uint32_t disp_lost(void) { return s_lost; }
void disp_backlight(int percent) { (void)percent; }
void disp_full_next(void) { s_full = 1; }

// a new picture every 5 s, but the glass is only touched if something in it changed
uint32_t disp_frame_ms(void) { return 5000; }

/* --- bit-bang, CS goes up after every byte like in ViewOwl --- */

static void wr_bus(uint8_t d)
{
    gpio_set_level(EPD_PIN_CS, 0);
    for (int i = 0; i < 8; i++) {
        gpio_set_level(EPD_PIN_SCK, 0);
        gpio_set_level(EPD_PIN_MOSI, (d & 0x80) ? 1 : 0);
        esp_rom_delay_us(SPI_DELAY_US);
        gpio_set_level(EPD_PIN_SCK, 1);           // sampled on the rising edge
        esp_rom_delay_us(SPI_DELAY_US);
        d <<= 1;
    }
    gpio_set_level(EPD_PIN_CS, 1);
}

static void wr_cmd(uint8_t c)
{
    gpio_set_level(EPD_PIN_DC, 0);
    wr_bus(c);
    gpio_set_level(EPD_PIN_DC, 1);
}

static void wr_data(uint8_t d)
{
    wr_bus(d);                                    // DC is already high
}

static void wait_busy(void)
{
    int waited = 0;
    while (gpio_get_level(EPD_PIN_BUSY) == 1) {   // high = busy
        vTaskDelay(pdMS_TO_TICKS(10));
        if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();   // a refresh takes seconds, its not a hang
        waited += 10;
        if (waited >= BUSY_TIMEOUT_MS) {
            ESP_LOGE(TAG, "BUSY stuck high for %d ms, check GPIO%d", waited, EPD_PIN_BUSY);
            s_lost++;
            return;
        }
    }
}

// 2 bit code of pixel x,y of our frame
static inline uint8_t code_at(int x, int y)
{
    if (x >= GFX_W || y >= GFX_H) return 3;
    return (uint8_t)((s_frame[y * PAPER_STRIDE + (x >> 2)] >> (6 - 2 * (x & 3))) & 3);
}

// 8 pixels from x0 on, one bit of their codes each (bit 0 for RAM 0x24, bit 1 for 0x26)
static uint8_t plane8(int x0, int y, int bit, int flip)
{
    uint8_t out = 0;
    for (int k = 0; k < 8; k++) {
        int x = x0 + k;
        uint8_t c = flip ? code_at(GFX_W - 1 - x, GFX_H - 1 - y) : code_at(x, y);
        out = (uint8_t)(out << 1 | ((c >> bit) & 1));
    }
    return out;
}

static inline uint8_t shown_at(int x, int y)
{
    if (x >= GFX_W || y >= GFX_H) return 3;
    return (uint8_t)((s_shown[y * PAPER_STRIDE + (x >> 2)] >> (6 - 2 * (x & 3))) & 3);
}

/* black and white for the partial refresh: white and light grey count as white.
 * the panel gets the new picture and the old one, and repaints only the pixels where the
 * two differ. greys where nothing changed stay */
static inline int bw_new(int x, int y) { return code_at(x, y) >= 2; }
static inline int bw_old(int x, int y) { return shown_at(x, y) >= 2; }

/* what a partial refresh would change (black/white flips), and how many pixels would still
 * show the wrong grey after it: a grey that came or went where black/white stays the same,
 * or a pixel that flips but should end up grey */
static void count_changes(int *flips, int *debt)
{
    int f = 0, d = 0;
    for (int i = 0; i < FRAME_BYTES; i++) {
        uint8_t a = s_frame[i], b = s_shown[i];
        if (a == b) continue;
        for (int k = 0; k < 4; k++, a <<= 2, b <<= 2) {
            int cn = a >> 6, cs = b >> 6;
            if (cn == cs) continue;
            int flip = (cn >= 2) != (cs >= 2);
            f += flip;
            int after = flip ? (cn >= 2 ? 3 : 0) : cs;     // what the glass would show after a partial
            d += after != cn;
        }
    }
    *flips = f;
    *debt = d;
}

// the model after a partial: flipped pixels are now plain black or white
static void after_partial(void)
{
    for (int i = 0; i < FRAME_BYTES; i++) {
        uint8_t a = s_frame[i], b = s_shown[i], out = 0;
        for (int k = 0; k < 4; k++, a <<= 2, b <<= 2) {
            int cn = a >> 6, cs = b >> 6;
            int c = (cn >= 2) != (cs >= 2) ? (cn >= 2 ? 3 : 0) : cs;
            out = (uint8_t)(out << 2 | c);
        }
        s_shown[i] = out;
    }
}

static void load_lut(const uint8_t *lut)
{
    wr_cmd(0x32);                                 // grey waveform, 227 bytes
    for (int i = 0; i < 227; i++) wr_data(lut[i]);
    wr_cmd(0x3F); wr_data(lut[227]);              // LUT end option
    wr_cmd(0x03); wr_data(lut[228]);              // gate voltage
    wr_cmd(0x04); wr_data(lut[229]); wr_data(lut[230]); wr_data(lut[231]);   // source VSH1/VSH2/VSL
    wr_cmd(0x2C); wr_data(lut[232]);              // VCOM
}

#if RADAR_DISPLAY == DISPLAY_EPD_400x300

#define EPD_W 400
#define EPD_H 300

static const uint8_t LUT_4GRAY[233] = {
    0x01, 0x0A, 0x1B, 0x0F, 0x03, 0x01, 0x01,
    0x05, 0x0A, 0x01, 0x0A, 0x01, 0x01, 0x01,
    0x05, 0x08, 0x03, 0x02, 0x04, 0x01, 0x01,
    0x01, 0x04, 0x04, 0x02, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x0A, 0x1B, 0x0F, 0x03, 0x01, 0x01,
    0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01,
    0x05, 0x48, 0x03, 0x82, 0x84, 0x01, 0x01,
    0x01, 0x84, 0x84, 0x82, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x0A, 0x1B, 0x8F, 0x03, 0x01, 0x01,
    0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01,
    0x05, 0x48, 0x83, 0x82, 0x04, 0x01, 0x01,
    0x01, 0x04, 0x04, 0x02, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x8A, 0x1B, 0x8F, 0x03, 0x01, 0x01,
    0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01,
    0x05, 0x48, 0x83, 0x02, 0x04, 0x01, 0x01,
    0x01, 0x04, 0x04, 0x02, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x8A, 0x9B, 0x8F, 0x03, 0x01, 0x01,
    0x05, 0x4A, 0x01, 0x8A, 0x01, 0x01, 0x01,
    0x05, 0x48, 0x03, 0x42, 0x04, 0x01, 0x01,
    0x01, 0x04, 0x04, 0x42, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x07, 0x17, 0x41, 0xA8,
    0x32, 0x30,
};

static void reset(void)
{
    gpio_set_level(EPD_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(EPD_PIN_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void window_all(void)
{
    wr_cmd(0x44); wr_data(0); wr_data((EPD_W - 1) >> 3);          // X in bytes
    wr_cmd(0x45); wr_data(0); wr_data(0); wr_data((EPD_H - 1) & 255); wr_data((EPD_H - 1) >> 8);
    wr_cmd(0x4E); wr_data(0);
    wr_cmd(0x4F); wr_data(0); wr_data(0);
}

static void init_4gray(void)
{
    reset();
    wait_busy();
    wr_cmd(0x12);                                 // soft reset
    wait_busy();
    wr_cmd(0x21); wr_data(0x00); wr_data(0x00);   // display update control 1
    wr_cmd(0x3C); wr_data(0x03);                  // border
    wr_cmd(0x0C); wr_data(0x8B); wr_data(0x9C); wr_data(0xA4); wr_data(0x0F);   // booster soft start
    load_lut(LUT_4GRAY);
    wr_cmd(0x11); wr_data(0x03);                  // X+, Y+
    window_all();
    wait_busy();
}

static void push(void)
{
    init_4gray();
    for (int bit = 0; bit < 2; bit++) {
        window_all();
        wr_cmd(bit ? 0x26 : 0x24);
        for (int y = 0; y < EPD_H; y++)
            for (int x = 0; x < EPD_W; x += 8) wr_data(plane8(x, y, bit, 0));
    }
    wr_cmd(0x22); wr_data(0xCF);                  // show it with the grey LUT
    wr_cmd(0x20);
    wait_busy();
}

/* drawing on top: ViewOwl's partial (its Class-C player runs on it) plus the old picture
 * in RAM 0x26, so unchanged pixels get no drive at all. 0xFF makes the panel load its own
 * fast waveform again, the grey one from push() is still in the registers */
static void push_partial(void)
{
    wr_cmd(0x3C); wr_data(0x80);
    wr_cmd(0x21); wr_data(0x00); wr_data(0x00);
    wr_cmd(0x3C); wr_data(0x80);
    wr_cmd(0x11); wr_data(0x03);
    for (int ram = 0; ram < 2; ram++) {           // 0x24 the new picture, 0x26 the one on the glass
        window_all();
        wr_cmd(ram ? 0x26 : 0x24);
        for (int y = 0; y < EPD_H; y++)
            for (int x = 0; x < EPD_W; x += 8) {
                uint8_t b = 0;
                for (int k = 0; k < 8; k++) b = (uint8_t)(b << 1 | (ram ? bw_old(x + k, y) : bw_new(x + k, y)));
                wr_data(b);
            }
    }
    wr_cmd(0x22); wr_data(0xFF);
    wr_cmd(0x20);
    wait_busy();
}

#else   /* 792x272, two controllers */

#define EPD_W 792
#define EPD_H 272

static const uint8_t LUT_4GRAY[233] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x4A, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x82, 0x42, 0x00, 0x00, 0x10, 0x00,
    0x01, 0x8A, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x41, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x82, 0x42, 0x00, 0x00, 0x10, 0x00,
    0x01, 0x81, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x81, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x82, 0x42, 0x00, 0x00, 0x10, 0x00,
    0x01, 0x41, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x8A, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x01, 0x82, 0x42, 0x00, 0x00, 0x10, 0x00,
    0x01, 0x4A, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00,                             // FR, XON
    0x22, 0x17, 0x41, 0xA8, 0x32, 0x40,           // EOPT VGH VSH1 VSH2 VSL VCOM
};

static void init_4gray(void)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    wait_busy();
    wr_cmd(0x12);                                 // soft reset, reaches both
    wait_busy();
    wr_cmd(0x0C); wr_data(0x8B); wr_data(0x9C); wr_data(0xA6); wr_data(0x0F);   // booster soft start
    wr_cmd(0x3C); wr_data(0x81);                  // border
    wait_busy();
    // master: rows, X up. Y from 271 down to 0
    wr_cmd(0x11); wr_data(0x01);
    wr_cmd(0x44); wr_data(0x00); wr_data(0x31);
    wr_cmd(0x45); wr_data(0x0F); wr_data(0x01); wr_data(0x00); wr_data(0x00);
    wr_cmd(0x4E); wr_data(0x00);
    wr_cmd(0x4F); wr_data(0x0F); wr_data(0x01);
    wait_busy();
    // slave: same thru 0x80 opcodes, X mirrored (0x91 is its data entry register)
    wr_cmd(0x91); wr_data(0x00);
    wr_cmd(0xC4); wr_data(0x31); wr_data(0x00);
    wr_cmd(0xC5); wr_data(0x0F); wr_data(0x01); wr_data(0x00); wr_data(0x00);
    wr_cmd(0xCE); wr_data(0x31);
    wr_cmd(0xCF); wr_data(0x0F); wr_data(0x01);
    load_lut(LUT_4GRAY);
}

/* four whole planes one after the other, no cursor moves in between. master gets panel
 * columns 0..399, slave 392..791 (8 hide behind the seam). with this addressing the
 * picture comes out turned 180 degrees, so we read our frame flipped */
static void push(void)
{
    init_4gray();
    const int bytes = 50;                         // 400 px of a controller row
    const uint8_t cmds[4] = { 0x24, 0x26, 0xA4, 0xA6 };
    for (int p = 0; p < 4; p++) {
        int bit = p & 1, col0 = p < 2 ? 0 : (bytes - 1) * 8;
        wr_cmd(cmds[p]);
        for (int y = 0; y < EPD_H; y++)
            for (int i = 0; i < bytes; i++) wr_data(plane8(col0 + i * 8, y, bit, 1));
    }
    wr_cmd(0x22); wr_data(0xCF);                  // show it with the grey LUT
    wr_cmd(0x20);
    vTaskDelay(pdMS_TO_TICKS(100));
    wait_busy();
}

/* drawing on top, the cascade. NOTE: ViewOwl never had a partial refresh on this panel,
 * this is the first try: the mono addressing of ViewOwl's epd_wide.c (columns, Y down,
 * slave X mirrored, 8 px seam, turned 180), new picture in 0x24/0xA4, the one on the glass
 * in 0x26/0xA6, and the panel's own mode 2 waveform (0xFF) */
#define RAM_ROW 100                               // 800 px of RAM a row

// one byte of cascade RAM, column c (0..99) and line (0..271), from our frame
static uint8_t ram_byte(int c, int line, int old)
{
    uint8_t out = 0;
    for (int b = 0; b < 8; b++) {
        int xp = RAM_ROW * 8 - 1 - (c * 8 + b), y = EPD_H - 1 - line, v = 1;
        int x = xp < 396 ? xp : xp >= 404 ? xp - 8 : -1;   // 396..403 sit behind the seam
        if (x >= 0) v = old ? bw_old(x, y) : bw_new(x, y);
        out = (uint8_t)(out << 1 | v);
    }
    return out;
}

static void stream(int col0, int old)
{
    for (int c = col0; c < col0 + 50; c++)
        for (int line = 0; line < EPD_H; line++) wr_data(ram_byte(c, line, old));
}

static void push_partial(void)
{
    vTaskDelay(pdMS_TO_TICKS(10));                // registers back to normal, the picture stays
    gpio_set_level(EPD_PIN_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    wait_busy();
    wr_cmd(0x12);
    wait_busy();
    wr_cmd(0x3C); wr_data(0x80);                  // leave the border alone
    // master
    wr_cmd(0x11); wr_data(0x05);
    wr_cmd(0x44); wr_data(0x00); wr_data(0x31);
    wr_cmd(0x45); wr_data(0x0F); wr_data(0x01); wr_data(0x00); wr_data(0x00);
    for (int ram = 0; ram < 2; ram++) {
        wr_cmd(0x4E); wr_data(0x00);
        wr_cmd(0x4F); wr_data(0x0F); wr_data(0x01);
        wr_cmd(ram ? 0x26 : 0x24);
        stream(0, ram);
    }
    // slave
    wr_cmd(0x91); wr_data(0x04);
    wr_cmd(0xC4); wr_data(0x31); wr_data(0x00);
    wr_cmd(0xC5); wr_data(0x0F); wr_data(0x01); wr_data(0x00); wr_data(0x00);
    for (int ram = 0; ram < 2; ram++) {
        wr_cmd(0xCE); wr_data(0x31);
        wr_cmd(0xCF); wr_data(0x0F); wr_data(0x01);
        wr_cmd(ram ? 0xA6 : 0xA4);
        stream(50, ram);
    }
    wr_cmd(0x22); wr_data(0xFF);
    wr_cmd(0x20);
    wait_busy();
}
#endif

void disp_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << EPD_PIN_SCK) | (1ULL << EPD_PIN_MOSI) | (1ULL << EPD_PIN_RES) |
                        (1ULL << EPD_PIN_DC) | (1ULL << EPD_PIN_CS) | (1ULL << EPD_PIN_PWR),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_config_t in = { .pin_bit_mask = 1ULL << EPD_PIN_BUSY, .mode = GPIO_MODE_INPUT };
    ESP_ERROR_CHECK(gpio_config(&in));
    gpio_set_level(EPD_PIN_PWR, 1);               // power the panel
    gpio_set_level(EPD_PIN_CS, 1);
    gpio_set_level(EPD_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
    int psram = 1;
    s_frame = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_shown = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_frame || !s_shown) {                   // no PSRAM on this board: internal RAM then
        psram = 0;
        if (!s_frame) s_frame = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_8BIT);
        if (!s_shown) s_shown = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_8BIT);
    }
    if (!s_frame || !s_shown) { ESP_LOGE(TAG, "no memory for the two frames"); return; }
    memset(s_frame, 0xFF, FRAME_BYTES);
    memset(s_shown, 0xFF, FRAME_BYTES);
    ESP_LOGI(TAG, "e-paper %dx%d, 4 greys, frames in %s, new picture every %u s", EPD_W, EPD_H,
             psram ? "PSRAM" : "internal RAM", (unsigned)(disp_frame_ms() / 1000));
}

void disp_strip(const canvas_t *cv)
{
    if (s_frame) paper_pack(cv, s_frame);
}

/* full grey refresh: new screen, every 10 min, or when too many greys are wrong (but not
 * more than once a minute for that). otherwise the black/white changes go on top. nothing
 * changed: the glass is left alone */
void disp_frame_end(void)
{
    if (!s_frame || !s_shown) return;
    int64_t t0 = esp_timer_get_time();
    int flips, debt;
    count_changes(&flips, &debt);
    int full = s_full || t0 - s_last_full >= FULL_EVERY_US || (debt > GREY_DEBT_MAX && t0 - s_last_full >= FULL_GAP_US);
    if (full) {
        push();
        memcpy(s_shown, s_frame, FRAME_BYTES);
        s_full = 0;
        s_last_full = t0;
        ESP_LOGI(TAG, "4-grey refresh, %d ms (%d px of grey were wrong)", (int)((esp_timer_get_time() - t0) / 1000), debt);
    } else if (flips) {
        push_partial();
        after_partial();
        ESP_LOGI(TAG, "drawn on top, %d px, %d ms, grey debt %d", flips, (int)((esp_timer_get_time() - t0) / 1000), debt);
    }
}

#endif
