#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    const char *ssid;
    const char *password;
    bool power_save;
} wifi_manager_config_t;

esp_err_t wifi_manager_start_sta(const wifi_manager_config_t *config);
bool wifi_manager_is_connected(void);
