#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*ws_client_rx_cb_t)(const uint8_t *data, size_t len, uint32_t crc32, void *ctx);

typedef struct {
    const char *uri;
    const char *auth_token;
    const uint8_t *ca_cert;
    size_t ca_cert_len;
    bool skip_common_name_check;
    uint32_t reconnect_min_delay_ms;
    uint32_t reconnect_max_delay_ms;
} ws_client_config_t;

esp_err_t ws_client_start(const ws_client_config_t *config, ws_client_rx_cb_t cb, void *ctx);
void ws_client_stop(void);
esp_err_t ws_client_send(const uint8_t *data, size_t len);
bool ws_client_is_connected(void);
