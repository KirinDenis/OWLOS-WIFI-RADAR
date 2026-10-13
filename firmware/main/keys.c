#include "keys.h"
#include "board.h"
#include "ui.h"

#ifdef PIN_KEY_OK
#include "driver/gpio.h"

/* all active low with the internal pull-up (Elecrow's example says the same, and a board
 * without the part then reads a steady "not pressed" instead of noise).
 * a key counts once it reads the same for 30 ms. OK and MENU have a long press (700 ms,
 * the 360 turn), so those two fire on release; the rest fire when pressed.
 * MENU long matters: some of these boards are used with just the two side keys */
#define STABLE_MS 30u
#define LONG_MS   700u

static const struct { int pin, key, longkey; } KEYS[] = {
    { PIN_KEY_UP, KEY_UP, 0 }, { PIN_KEY_DOWN, KEY_DOWN, 0 }, { PIN_KEY_OK, KEY_OK, KEY_OK_LONG },
    { PIN_KEY_MENU, KEY_MENU, KEY_OK_LONG }, { PIN_KEY_BACK, KEY_BACK, 0 },
};
#define NK (int)(sizeof KEYS / sizeof KEYS[0])

static uint8_t  s_raw[NK], s_down[NK], s_long[NK];
static uint32_t s_change[NK], s_since[NK];

void keys_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < NK; i++) mask |= 1ULL << KEYS[i].pin;
    gpio_config_t c = { .pin_bit_mask = mask, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&c);
}

int keys_poll(uint32_t now)
{
    int ev = 0;
    for (int i = 0; i < NK; i++) {
        uint8_t raw = gpio_get_level(KEYS[i].pin) == 0;
        if (raw != s_raw[i]) { s_raw[i] = raw; s_change[i] = now; }
        if (raw == s_down[i] || now - s_change[i] < STABLE_MS) {
            // held: a key with a long action fires it once
            if (s_down[i] && KEYS[i].longkey && !s_long[i] && now - s_since[i] >= LONG_MS) { s_long[i] = 1; ev = KEYS[i].longkey; }
            continue;
        }
        s_down[i] = raw;
        if (KEYS[i].longkey) {
            if (raw) { s_since[i] = now; s_long[i] = 0; }
            else if (!s_long[i]) ev = KEYS[i].key;   // short press counts on release
        } else if (raw) {
            ev = KEYS[i].key;
        }
    }
    return ev;
}

#else

void keys_init(void) { }
int keys_poll(uint32_t now) { (void)now; return 0; }

#endif
