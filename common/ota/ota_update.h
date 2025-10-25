#pragma once

#include "esp_app_format.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef esp_err_t (*ota_update_validate_cb_t)(const esp_app_desc_t *new_app,
                                              const esp_app_desc_t *running_app,
                                              const uint8_t new_app_sha256[32],
                                              const char *stored_version,
                                              void *ctx);

typedef struct {
    const char *url;
    const uint8_t *cert_pem;
    size_t cert_len;
    bool auto_reboot;
    uint32_t check_delay_ms;
    bool (*wait_for_connectivity)(uint32_t timeout_ms);
    ota_update_validate_cb_t validate;
    void *validate_ctx;
    uint32_t initial_backoff_ms;
    uint32_t max_backoff_ms;
    uint8_t max_retries;
    const char *nvs_namespace;
    const char *nvs_key;
} ota_update_config_t;

esp_err_t ota_update_schedule(const ota_update_config_t *config);
void ota_update_task_entry(void *arg);
