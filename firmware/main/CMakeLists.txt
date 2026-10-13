# every board's driver is in here, each file compiles to nothing unless board.h picks it
idf_component_register(SRCS "main.c" "radio.c" "button.c" "keys.c" "touch.c" "settings.c" "flashstore.c"
                            "lcd_spi.c" "lcd_ili9486.c" "epd.c" "i2c_bus.c"
                       INCLUDE_DIRS "."
                       REQUIRES core esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc esp_driver_i2c
                                esp_wifi esp_event nvs_flash esp_timer esp_partition)
