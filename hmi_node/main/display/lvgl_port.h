#pragma once

#include "esp_err.h"
#include "lvgl.h"

esp_err_t lvgl_port_init(void);
void lvgl_port_lock(void);
void lvgl_port_unlock(void);

