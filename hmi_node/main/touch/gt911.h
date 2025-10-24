#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t x;
    uint16_t y;
    bool pressed;
} gt911_touch_data_t;

esp_err_t gt911_init(void);
bool gt911_read_touch(gt911_touch_data_t *data);
