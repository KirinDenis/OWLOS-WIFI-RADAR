/*
 * pins and parts of every board, picked by RADAR_DISPLAY (core/config.h).
 * wiring is the same as in ViewOwl's firmware for that display, change pins only here.
 * what is checked on hardware and what is not, is written next to each board.
 */
#pragma once
#include "config.h"

/* how the picture gets out:
 *   DISP_LCD_SPI   esp_lcd SPI panel IO, 8 bit commands (ILI9341, ST7796, GC9A01)
 *   DISP_ILI9486   raw SPI, every byte padded to 16 bits (RPi LCD with shift registers)
 *   DISP_EPD       SSD1683 e-paper, bit-banged, 1 bit a pixel (core/paper.c)
 * touch: TOUCH_XPT_BITBANG, TOUCH_XPT_SPI (on the LCD bus), TOUCH_CST816, or none */

#if RADAR_DISPLAY == DISPLAY_ILI9341_320x240
/* ESP32-2432S028R "cheap yellow display". the one this was written on, all of it works */
#define DISP_LCD_SPI   1
#define LCD_CHIP_ILI9341 1
#define PIN_LCD_SCLK  14
#define PIN_LCD_MOSI  13
#define PIN_LCD_MISO  (-1)      // write only, leave strapping pin 12 alone
#define PIN_LCD_DC     2
#define PIN_LCD_CS    15
#define PIN_LCD_RST   (-1)      // tied high, so software reset only
#define PIN_LCD_BL    21        // backlight, PWM
#define LCD_SPI_HZ    (40 * 1000 * 1000)   // 80 MHz APB / 2. ViewOwl asks for 50, the divider ends up here anyway
#define LCD_MADCTL    0xE8      // landscape for this panel: MY|MX|MV|BGR, same as ViewOwl
#define DISP_STRIP    24        // rows a strip: 240 / 24 = 10 strips
/* XPT2046 has its own pins here, we bit-bang it, cheap and the LCD bus stays free.
 * 36 and 39 are input only pins */
#define TOUCH_XPT_BITBANG 1
#define PIN_T_CLK     25
#define PIN_T_MOSI    32
#define PIN_T_MISO    39
#define PIN_T_CS      33
#define PIN_T_IRQ     36
#define PIN_BUTTON     0        // BOOT
// TODO: board speaker for geiger like clicks while hunting
/* the same name is sold in three revisions with the same pins and three different screens
 * (rzeldent/platformio-espressif32-sunton, esp32-2432S028R / Rv2 / Rv3):
 *   v1  micro USB      ILI9341, BGR, our MADCTL 0xE8, no inversion. the one this was written on
 *   v2  USB-C          ILI9341 with its own power and gamma values, colour inversion on
 *   v3  "7789"         ST7789, RGB, mirrored on one axis against v1, inversion on
 * nobody knows which one he has, so all three are in this build (lcd_spi.c, LCD_PROFILES), v1
 * first: it is what this firmware always did. BOOT picks another one, see main.c */
#define LCD_PROFILES   3

#elif RADAR_DISPLAY == DISPLAY_ST7796_480x320
/* ESP32-3248S035 (3.5" ST7796), ViewOwl's 480x320 build. runs (2026-10-13): ~8 fps, 24 KB
 * heap left. the touch is the R version, XPT2046 on the LCD bus (CS 33), works */
#define DISP_LCD_SPI   1
#define LCD_CHIP_ST7796 1
#define PIN_LCD_SCLK  14
#define PIN_LCD_MOSI  13
#define PIN_LCD_MISO  12        // only the touch reads it
#define PIN_LCD_DC     2
#define PIN_LCD_CS    15
#define PIN_LCD_RST   (-1)
#define PIN_LCD_BL    27
#define LCD_SPI_HZ    (40 * 1000 * 1000)
#define LCD_MADCTL    0xE8      // MY|MX|MV|BGR, ViewOwl lcd_init.c
#define DISP_STRIP    12        // 480 wide and no PSRAM: half height strips, or the heap runs out
#define TOUCH_XPT_SPI 1
#define PIN_T_CS      33
#define PIN_T_IRQ     36
#define PIN_BUTTON     0

#elif RADAR_DISPLAY == DISPLAY_ILI9486_480x320
/* Waveshare RPi LCD (A) 3.5" on the OWLOSAirQuality board, ViewOwl's ILI9486 build.
 * NOT run on hardware yet. no touch wired that ViewOwl knows of, BOOT button only */
#define DISP_ILI9486   1
#define PIN_LCD_SCLK  18
#define PIN_LCD_MOSI  23
#define PIN_LCD_MISO  19
#define PIN_LCD_DC     2
#define PIN_LCD_CS    15        // driven by hand, CS has to go up after every padded byte
#define PIN_LCD_RST    4
#define LCD_SPI_HZ    (20 * 1000 * 1000)   // the 74HC shift registers on the board top out here
#define LCD_MADCTL    0x28      // MV|BGR, ViewOwl's sweep found this one
#define DISP_STRIP    12
#define PIN_BUTTON     0

#elif RADAR_DISPLAY == DISPLAY_GC9A01_240x240
/* Elecrow CrowPanel 1.28" round, ESP32-C3 (4 MB on the one here). ViewOwl's C3 build.
 * runs (2026-10-13): ~8 fps, 31 KB heap left. reset and backlight are on a PI4IOE5V6408
 * I2C expander, not on GPIOs. the touch chip is not named anywhere in ViewOwl, CST816 at
 * 0x15 was a guess and it answered; touch.c still goes on without touch if nothing does */
#define DISP_LCD_SPI   1
#define LCD_CHIP_GC9A01 1
#define PIN_LCD_SCLK   6
#define PIN_LCD_MOSI   7
#define PIN_LCD_MISO  (-1)
#define PIN_LCD_DC     2
#define PIN_LCD_CS    10
#define PIN_LCD_RST   (-1)      // on the expander
#define LCD_SPI_HZ    (80 * 1000 * 1000)   // C3 IOMUX pins, ViewOwl runs 80. snow on screen = go to 40
#define LCD_MADCTL    0x68      // MV|MX|BGR, ViewOwl's lcd_gc9a01.c (0x28 came out mirrored)
#define DISP_STRIP    24
#define PIN_I2C_SDA    4
#define PIN_I2C_SCL    5
#define IOEX_ADDR      0x43
#define IOEX_BIT_BL    (1 << 2)
#define IOEX_BIT_TRST  (1 << 3)  // touch reset
#define IOEX_BIT_RST   (1 << 4)  // LCD reset
#define TOUCH_CST816   1
#define CST816_ADDR    0x15
#define PIN_BUTTON     9        // BOOT on the C3

#elif RADAR_DISPLAY == DISPLAY_EPD_400x300 || RADAR_DISPLAY == DISPLAY_EPD_792x272
/* Elecrow CrowPanel e-paper 4.2" (400x300) and 5.79" (792x272), ESP32-S3. NOT run on
 * hardware yet: panel pins and command sequences are ViewOwl's (ViewOwl.ESP32_EPD.Client),
 * the dial and key pins come from Elecrow's 5.79 key example, the 4.2 is assumed the same */
#define DISP_EPD       1
#define EPD_PIN_SCK   12
#define EPD_PIN_MOSI  11
#define EPD_PIN_RES   47
#define EPD_PIN_DC    46
#define EPD_PIN_CS    45
#define EPD_PIN_BUSY  48
#define EPD_PIN_PWR    7        // panel power, has to be high
#define DISP_STRIP     8        // 792 x 24 rows x 4 bytes was 76 KB in one piece, not there after wifi. paper is slow anyway
#define PIN_KEY_UP     6        // dial up
#define PIN_KEY_DOWN   4        // dial down
#define PIN_KEY_OK     5        // dial pressed
#define PIN_KEY_MENU   2
#define PIN_KEY_BACK   1
#define PIN_BUTTON     0

#endif

#ifndef PIN_LCD_BL
#define PIN_LCD_BL (-1)
#endif

// raw touch units across the screen, for the sanity check of a stored alignment
#if TOUCH_CST816
#define TOUCH_RAW_SPAN 240
#else
#define TOUCH_RAW_SPAN 3700
#endif
