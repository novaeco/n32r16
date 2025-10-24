#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

esp_err_t pca9685_init(uint8_t addr, uint16_t frequency_hz);
esp_err_t pca9685_set_pwm(uint8_t addr, uint8_t channel, uint16_t duty);
esp_err_t pca9685_set_all(uint8_t addr, const uint16_t *duty, size_t count);
