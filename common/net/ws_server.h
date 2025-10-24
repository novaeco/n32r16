#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t port;
    size_t max_clients;
    const char *auth_token;
    const uint8_t *server_cert;
    size_t server_cert_len;
    const uint8_t *server_key;
    size_t server_key_len;
    uint32_t ping_interval_ms;
    uint32_t pong_timeout_ms;
    size_t rx_buffer_size;
} ws_server_config_t;

typedef void (*ws_server_rx_cb_t)(const uint8_t *data, size_t len, uint32_t crc32, void *ctx);

esp_err_t ws_server_start(const ws_server_config_t *config, ws_server_rx_cb_t cb, void *ctx);
void ws_server_stop(void);
esp_err_t ws_server_send(const uint8_t *data, size_t len);
