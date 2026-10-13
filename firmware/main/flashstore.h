#pragma once
#include "store.h"

/* state saved in ESP32's own flash: partition "radar" (512 KB), a ring of 12 KB slots.
 * each save goes to the next slot, so a sector is erased once per round. with a save
 * every 3 min thats about once in 2 hours, flash is rated 100k erases = decades */
int fstore_init(void);
int fstore_load(map_t *m, saved_net_t *nets, int *n);
int fstore_save(const map_t *m, const saved_net_t *nets, int n);
