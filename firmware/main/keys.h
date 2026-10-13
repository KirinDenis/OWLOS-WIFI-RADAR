#pragma once
#include <stdint.h>

/* the dial (up, down, press) and MENU / BACK keys of the e-paper boards.
 * returns one KEY_* from ui.h per press, 0 when nothing happened. boards without them
 * always get 0. BOOT is separate, see button.h */
void keys_init(void);
int  keys_poll(uint32_t now_ms);
