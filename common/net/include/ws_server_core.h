#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

typedef void (*ws_server_rx_cb_t)(const uint8_t *data, size_t len);

typedef struct {
    uint16_t port;
    ws_server_rx_cb_t on_rx;
} ws_server_config_t;

esp_err_t ws_server_start(const ws_server_config_t *cfg);
esp_err_t ws_server_send(const uint8_t *data, size_t len);
void ws_server_stop(void);

