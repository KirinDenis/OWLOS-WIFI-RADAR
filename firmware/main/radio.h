#pragma once
#include "app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// starts wifi in listen only mode + the task that hops channels and feeds
// what it hears into *app (holding *lock)
void     radio_start(app_t *app, SemaphoreHandle_t lock);
uint32_t radio_dropped(void);    // frames dropped because the queue was full
