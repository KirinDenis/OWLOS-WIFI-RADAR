#pragma once
#include "ui.h"

// kept in NVS across reboots: touch calibration and the color theme
int  settings_load_cal(touch_cal_t *cal);      // 1 if there was one saved
void settings_save_cal(const touch_cal_t *cal);
int  settings_load_theme(void);
void settings_save_theme(int theme);
int  settings_load_screen(void);               // screen start-up profile, 0 if never picked
void settings_save_screen(int profile);
