#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *salt;
    size_t salt_len;
    const uint8_t *verifier;
    size_t verifier_len;
} wifi_manager_sec2_params_t;

typedef struct {
    bool power_save;
    const char *service_name_suffix;
    const char *pop;
    const char *service_key;
    bool prefer_ble;
    bool force_provisioning;
    uint32_t provisioning_timeout_ms;
    uint32_t connect_timeout_ms;
    uint8_t max_connect_attempts;
    const wifi_manager_sec2_params_t *sec2_params;
    const char *sec2_username;
} wifi_manager_config_t;

esp_err_t wifi_manager_start(const wifi_manager_config_t *config);
esp_err_t wifi_manager_prepare_provisioning(const wifi_manager_config_t *config, bool *using_ble);
bool wifi_manager_is_connected(void);
void wifi_manager_request_reprovision(void);
