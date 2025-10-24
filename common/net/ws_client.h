#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef void (*ws_client_rx_cb_t)(const uint8_t *data, size_t len, uint32_t crc32, void *ctx);

typedef struct {
    const char *uri;
    bool use_ssl;
} ws_client_config_t;

esp_err_t ws_client_start(const ws_client_config_t *config, ws_client_rx_cb_t cb, void *ctx);
void ws_client_stop(void);
esp_err_t ws_client_send(const uint8_t *data, size_t len);
bool ws_client_is_connected(void);
