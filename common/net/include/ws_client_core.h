#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef void (*ws_client_rx_cb_t)(const uint8_t *data, size_t len);

typedef enum {
    WS_CLIENT_EVENT_CONNECTING = 0,
    WS_CLIENT_EVENT_CONNECTED,
    WS_CLIENT_EVENT_DISCONNECTED,
    WS_CLIENT_EVENT_ERROR,
} ws_client_event_t;

typedef void (*ws_client_event_cb_t)(ws_client_event_t event, esp_err_t reason);

typedef struct {
    const char *uri;
    const char *cert_pem;
    ws_client_rx_cb_t on_rx;
    uint32_t reconnect_min_ms;
    uint32_t reconnect_max_ms;
} ws_client_config_t;

esp_err_t ws_client_start(const ws_client_config_t *cfg);
esp_err_t ws_client_send(const uint8_t *data, size_t len);
void ws_client_register_event_callback(ws_client_event_cb_t cb);
void ws_client_stop(void);

