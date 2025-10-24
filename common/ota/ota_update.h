#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *url;
    const uint8_t *cert_pem;
    size_t cert_len;
    bool auto_reboot;
    uint32_t check_delay_ms;
    bool (*wait_for_connectivity)(uint32_t timeout_ms);
} ota_update_config_t;

esp_err_t ota_update_schedule(const ota_update_config_t *config);
