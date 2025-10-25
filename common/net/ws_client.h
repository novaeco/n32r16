#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
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

typedef struct {
    esp_websocket_client_handle_t (*client_init)(const esp_websocket_client_config_t *config);
    esp_err_t (*client_start)(esp_websocket_client_handle_t client);
    esp_err_t (*client_stop)(esp_websocket_client_handle_t client);
    void (*client_destroy)(esp_websocket_client_handle_t client);
    const char *(*client_get_uri)(esp_websocket_client_handle_t client);
    int (*client_send_bin)(esp_websocket_client_handle_t client, const char *data, size_t len, TickType_t timeout);
    esp_err_t (*register_events)(esp_websocket_client_handle_t client, esp_websocket_event_id_t event,
                                 esp_event_handler_t handler, void *handler_args);
    TimerHandle_t (*timer_create)(const char *name, TickType_t period, UBaseType_t auto_reload, void *timer_id,
                                  TimerCallbackFunction_t callback);
    BaseType_t (*timer_start)(TimerHandle_t timer, TickType_t ticks_to_wait);
    BaseType_t (*timer_stop)(TimerHandle_t timer, TickType_t ticks_to_wait);
    BaseType_t (*timer_delete)(TimerHandle_t timer, TickType_t ticks_to_wait);
    BaseType_t (*timer_change_period)(TimerHandle_t timer, TickType_t new_period, TickType_t ticks_to_wait);
} ws_client_platform_t;

esp_err_t ws_client_start(const ws_client_config_t *config, ws_client_rx_cb_t cb, void *ctx);
void ws_client_stop(void);
esp_err_t ws_client_send(const uint8_t *data, size_t len);
bool ws_client_is_connected(void);
void ws_client_set_platform(const ws_client_platform_t *platform);
