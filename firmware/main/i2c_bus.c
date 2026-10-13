#include "i2c_bus.h"
#ifdef PIN_I2C_SDA
#include "esp_err.h"

static i2c_master_bus_handle_t s_bus;

static void bus_up(void)
{
    if (s_bus) return;
    i2c_master_bus_config_t cfg = {
        .i2c_port = -1,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
}

i2c_master_dev_handle_t i2c_dev(uint8_t addr)
{
    bus_up();
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    i2c_master_dev_handle_t h = NULL;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev, &h));
    return h;
}

int i2c_probe(uint8_t addr)
{
    bus_up();
    return i2c_master_probe(s_bus, addr, 50) == ESP_OK;
}
#endif
