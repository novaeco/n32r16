#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WS_SERVER_MAX_CLIENTS
#define WS_SERVER_MAX_CLIENTS 4
#endif

typedef void (*ws_server_rx_cb_t)(const uint8_t *data, size_t len);

typedef enum {
    WS_SERVER_CLIENT_CONNECTED = 0,
    WS_SERVER_CLIENT_DISCONNECTED,
} ws_server_client_event_t;

typedef void (*ws_server_client_event_cb_t)(int fd, ws_server_client_event_t event);

typedef struct {
    uint16_t port;
    ws_server_rx_cb_t on_rx;
    ws_server_client_event_cb_t on_client_event;
    const uint8_t *server_cert;
    size_t server_cert_len;
    const uint8_t *private_key;
    size_t private_key_len;
} ws_server_config_t;

esp_err_t ws_server_start(const ws_server_config_t *cfg);
esp_err_t ws_server_send(const uint8_t *data, size_t len);
esp_err_t ws_server_send_to(int fd, const uint8_t *data, size_t len);
size_t ws_server_active_client_count(void);
void ws_server_stop(void);

#ifdef __cplusplus
}
#endif

