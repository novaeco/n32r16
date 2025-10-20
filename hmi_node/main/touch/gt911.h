#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

esp_err_t gt911_init(void);
void gt911_register_input_device(void);

