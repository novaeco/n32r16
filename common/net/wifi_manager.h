#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    bool power_save;
    const char *service_name_suffix;
    const char *pop;
    const char *service_key;
    bool prefer_ble;
    bool force_provisioning;
} wifi_manager_config_t;

esp_err_t wifi_manager_start(const wifi_manager_config_t *config);
bool wifi_manager_is_connected(void);
void wifi_manager_request_reprovision(void);
