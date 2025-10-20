#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define DS18B20_MAX_SENSORS 4

typedef struct {
    uint64_t rom_code;
    float temperature_c;
    bool valid;
} ds18b20_sample_t;

esp_err_t ds18b20_scan(ds18b20_sample_t *sensors, size_t *count);
esp_err_t ds18b20_trigger_conversion(void);
esp_err_t ds18b20_read_scratchpad(ds18b20_sample_t *sensors, size_t count);

