# Third party stuff

- **Cascadia Mono** (c) Microsoft, SIL Open Font License 1.1. `core/font_small.c`, `font_medium.c`, `font_large.c` and `font_huge.c` are bitmaps made from it with `tools/FontBake`. https://github.com/microsoft/cascadia-code
- **DejaVu Sans Bold**, Bitstream Vera / DejaVu fonts license (free, bitmaps made from it may be shipped). `core/font_paper_*.c` are bitmaps of it for the e-paper screens, made with `tools/FontBake`. https://dejavu-fonts.github.io/
- **e-paper grey waveforms** (LUTs in `firmware/main/epd.c`) are from my ViewOwl firmware; ViewOwl's 5.79" one is a port of Waveshare's EPD_5in79 driver.
- **ILI9341 init values** in `firmware/main/lcd_spi.c` are the register values from Espressif's `esp_lcd_ili9341` component 1.2.0 (Apache 2.0). https://components.espressif.com/components/espressif/esp_lcd_ili9341
- **ST7796 init values** in `firmware/main/lcd_spi.c` are the vendor defaults from Espressif's `esp_lcd_st7796` component 1.3.5 (Apache 2.0). https://components.espressif.com/components/espressif/esp_lcd_st7796
- **GC9A01, ILI9486 and SSD1683 e-paper sequences** (`lcd_spi.c`, `lcd_ili9486.c`, `epd.c`) are taken over from my ViewOwl firmware, where they run on these panels. ViewOwl's ILI9486 gamma values come from TFT_eSPI's ILI9486 init, and its 5.79" cascade is a port of Elecrow's CrowPanel example.
- the **3x5 pixel font** in `core/font.c` is from my game OWL FLY (CITY.DAT).
