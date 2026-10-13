/*
 * store - what survives a power cycle: the walk map and known networks.
 * few KB: map struct as is, behind a header with version and sizes, so state from
 * another build is refused, not misread. on device it has its own flash partition
 * (firmware/main/flashstore.c).
 */
/* after restore the map just goes on: old walk joins the new one with a long gap so
 * solver can find where you stand now. left/right from an old turn stays, heading doesnt.
 * restored networks show faded and disappear unless they are heard again. */
#pragma once
#include <stdio.h>
#include "app.h"
#include "map.h"

typedef struct {
    uint8_t bssid[6];
    char    ssid[33];
    uint8_t channel, sec, hidden;
    int8_t  rssi, peak;
} saved_net_t;

// state as bytes: device keeps it in flash partition, PC preview in a file.
// store_size(n) = exact size for n networks
size_t store_size(int n);
size_t store_pack(uint8_t *buf, size_t cap, const map_t *m, const saved_net_t *nets, int n);
int    store_unpack(const uint8_t *buf, size_t len, map_t *m, saved_net_t *nets, int *n);

int  store_save(FILE *f, const map_t *m, const saved_net_t *nets, int n);
// 1 if a state was read, *n networks go to nets (needs room for MAX_CONTACTS)
int  store_load(FILE *f, map_t *m, saved_net_t *nets, int *n);

int  store_collect(const app_t *a, saved_net_t *nets);           // returns n
void store_restore(app_t *a, const saved_net_t *nets, int n, uint32_t now);
void store_csv(FILE *f, const saved_net_t *nets, int n);
