#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ssid[33];
    char password[65];
} wifi_sta_credentials_t;

typedef struct {
    const char *service_prefix;
    const char *service_key;
    const char *proof_of_possession;
    uint32_t timeout_ms;
} wifi_provisioner_config_t;

esp_err_t wifi_provisioner_acquire(const wifi_provisioner_config_t *cfg, wifi_sta_credentials_t *out);
void wifi_provisioner_shutdown(void);

#ifdef __cplusplus
}
#endif

