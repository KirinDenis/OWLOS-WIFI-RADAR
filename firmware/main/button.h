#pragma once
#include <stdint.h>

enum { BTN_NONE, BTN_SHORT, BTN_LONG };

void button_init(void);
/* call every frame. long press fires at 700 ms while still held, so the turn
 * starts without waiting for release. short press fires on release */
int  button_poll(uint32_t now_ms);
