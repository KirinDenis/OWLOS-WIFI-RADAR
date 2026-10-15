/* config.h - which display this build is for.
 * pick one here, or pass it from the build: BUILD.BAT <display> does -DRADAR_DISPLAY=...
 * pins and drivers for each one are in firmware/main/board.h */
#pragma once

#define DISPLAY_ILI9341_320x240  1   // ESP32-2432S028R "cheap yellow display", the first one
#define DISPLAY_ST7796_480x320   2   // 3.5" ST7796 on classic ESP32 (ViewOwl 480x320)
#define DISPLAY_ILI9486_480x320  3   // Waveshare RPi LCD (A) 3.5" on OWLOSAirQuality board
#define DISPLAY_GC9A01_240x240   4   // round 1.28", ESP32-C3
#define DISPLAY_EPD_400x300      5   // e-paper 4.2" SSD1683, ESP32-S3
#define DISPLAY_EPD_792x272      6   // wide e-paper 5.79", two SSD1683, ESP32-S3
#define DISPLAY_ST7789_320x240   7   // the 2.8" CYD with two USB ports (micro + C): an ST7789 instead of the ILI9341

#ifndef RADAR_DISPLAY
#define RADAR_DISPLAY DISPLAY_ILI9341_320x240
#endif

#if RADAR_DISPLAY == DISPLAY_ILI9341_320x240 || RADAR_DISPLAY == DISPLAY_ST7789_320x240
#define GFX_W 320
#define GFX_H 240
#elif RADAR_DISPLAY == DISPLAY_ST7796_480x320 || RADAR_DISPLAY == DISPLAY_ILI9486_480x320
#define GFX_W 480
#define GFX_H 320
#elif RADAR_DISPLAY == DISPLAY_GC9A01_240x240
#define GFX_W 240
#define GFX_H 240
#define RADAR_ROUND 1                // glass is a circle, nothing may sit in the corners
#elif RADAR_DISPLAY == DISPLAY_EPD_400x300
#define GFX_W 400
#define GFX_H 300
#define RADAR_EPAPER 1               // slow refresh, 4 greys, no animation
#elif RADAR_DISPLAY == DISPLAY_EPD_792x272
#define GFX_W 792
#define GFX_H 272
#define RADAR_EPAPER 1
#else
#error "unknown RADAR_DISPLAY, see config.h"
#endif

#ifndef RADAR_ROUND
#define RADAR_ROUND 0
#endif
#ifndef RADAR_EPAPER
#define RADAR_EPAPER 0
#endif

/* boards with touch. all of them go thru the 5 cross alignment once: the resistive ones
 * need it anyway, and on the round one it finds out how the touch chip's axes sit */
#define RADAR_TOUCH_CAL (RADAR_DISPLAY == DISPLAY_ILI9341_320x240 || RADAR_DISPLAY == DISPLAY_ST7789_320x240 || \
                         RADAR_DISPLAY == DISPLAY_ST7796_480x320 || RADAR_DISPLAY == DISPLAY_GC9A01_240x240)

#define RADAR_NAME "OWLOS RADAR"
