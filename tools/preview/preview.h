// shared by preview.c and walk.c: random numbers, render a frame, screenshot
#pragma once
#include "../../core/app.h"
#include "../../core/ui.h"

float frand(void);
float gauss(void);
void  render(ui_t *u, const app_t *a, uint32_t now);
void  shot(const char *dir, const char *name, int zoom);
int   walk_main(const char *dir, int still);
void  tap(ui_t *u, app_t *a, int x, int y, uint32_t t);
