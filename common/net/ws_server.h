#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef void (*ws_server_rx_cb_t)(const uint8_t *data, size_t len, uint32_t crc32, void *ctx);

esp_err_t ws_server_start(uint16_t port, ws_server_rx_cb_t cb, void *ctx);
void ws_server_stop(void);
esp_err_t ws_server_send(const uint8_t *data, size_t len);
