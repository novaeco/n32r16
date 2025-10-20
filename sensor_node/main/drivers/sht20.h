#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    float temperature_c;
    float humidity_rh;
    uint32_t timestamp_ms;
    bool valid;
} sht20_sample_t;

esp_err_t sht20_init(uint8_t address);
esp_err_t sht20_read(uint8_t address, sht20_sample_t *out_sample);

