#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint16_t duty_cycle[16];
    uint16_t frequency_hz;
} pca9685_state_t;

esp_err_t pca9685_init(uint8_t address, uint16_t frequency_hz);
esp_err_t pca9685_set_pwm(uint8_t address, uint8_t channel, uint16_t duty);
esp_err_t pca9685_snapshot(uint8_t address, pca9685_state_t *state);

