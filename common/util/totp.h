#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *secret;
    size_t secret_len;
    uint32_t period_s;
    uint8_t digits;
} totp_config_t;

esp_err_t totp_compute(const totp_config_t *cfg, uint64_t unix_time, uint32_t *code);
esp_err_t totp_verify(const totp_config_t *cfg, uint64_t unix_time, uint32_t window, uint32_t code, bool *match);

