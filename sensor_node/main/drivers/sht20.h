#pragma once

#include "esp_err.h"
#include <stdint.h>

esp_err_t sht20_soft_reset(uint8_t addr);
esp_err_t sht20_read_temperature_humidity(uint8_t addr, float *temperature_c, float *humidity);
