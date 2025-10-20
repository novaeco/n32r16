#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef void (*ws_client_rx_cb_t)(const uint8_t *data, size_t len, bool is_text);

typedef struct {
    const char *uri;
    ws_client_rx_cb_t on_rx;
} ws_client_config_t;

esp_err_t ws_client_start(const ws_client_config_t *cfg);
esp_err_t ws_client_send(const uint8_t *data, size_t len, bool is_text);
void ws_client_stop(void);

