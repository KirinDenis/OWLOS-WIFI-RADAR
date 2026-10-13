// BOOT button, polled from the screen task
#include "button.h"
#include "board.h"
#include "driver/gpio.h"

#define LONG_MS 700u            // long press

static int      s_down;
static uint32_t s_since;
static int      s_fired;

void button_init(void)
{
    gpio_config_t c = {
        .pin_bit_mask = 1ULL << PIN_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&c);
}

int button_poll(uint32_t now)
{
    int down = gpio_get_level(PIN_BUTTON) == 0;
    int ev = BTN_NONE;
    if (down && !s_down) {
        s_since = now;
        s_fired = 0;
    } else if (down && !s_fired && now - s_since >= LONG_MS) {
        s_fired = 1;
        ev = BTN_LONG;
    } else if (!down && s_down && !s_fired && now - s_since >= 30u) {
        ev = BTN_SHORT;                        // under 30 ms it's just contact bounce
    }
    s_down = down;
    return ev;
}
