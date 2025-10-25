#include "ws_client.h"

#include "esp_log.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ws_client";

static esp_websocket_client_handle_t s_client;
static ws_client_rx_cb_t s_rx_cb;
static void *s_rx_ctx;
static bool s_connected;
static bool s_should_run;
static TimerHandle_t s_reconnect_timer;
static uint32_t s_reconnect_delay_ms;
static uint32_t s_reconnect_min_ms;
static uint32_t s_reconnect_max_ms;
static char *s_auth_header;

/**
 * @brief Default platform hook to create a WebSocket client handle.
 *
 * @param config Pointer to the client configuration structure.
 * @return Handle to the initialized client or NULL on failure.
 */
static esp_websocket_client_handle_t client_init_default(const esp_websocket_client_config_t *config)
{
    return esp_websocket_client_init(config);
}

/**
 * @brief Default platform hook to start a WebSocket client connection.
 *
 * @param client Client handle returned from initialization.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t client_start_default(esp_websocket_client_handle_t client)
{
    return esp_websocket_client_start(client);
}

/**
 * @brief Default platform hook to stop an active WebSocket connection.
 *
 * @param client Client handle to stop.
 * @return ESP_OK on success or an error code.
 */
static esp_err_t client_stop_default(esp_websocket_client_handle_t client)
{
    return esp_websocket_client_stop(client);
}

/**
 * @brief Default platform hook to destroy a WebSocket client handle.
 *
 * @param client Client handle to destroy.
 * @return void
 */
static void client_destroy_default(esp_websocket_client_handle_t client)
{
    esp_websocket_client_destroy(client);
}

/**
 * @brief Default platform hook to query the active client URI.
 *
 * @param client Client handle.
 * @return Pointer to the URI string.
 */
static const char *client_get_uri_default(esp_websocket_client_handle_t client)
{
    return esp_websocket_client_get_uri(client);
}

/**
 * @brief Default platform hook to send binary data on the WebSocket.
 *
 * @param client Client handle.
 * @param data Pointer to the data buffer.
 * @param len Number of bytes to transmit.
 * @param timeout Maximum wait time in RTOS ticks.
 * @return Number of bytes sent or negative on error.
 */
static int client_send_bin_default(esp_websocket_client_handle_t client, const char *data, size_t len,
                                   TickType_t timeout)
{
    return esp_websocket_client_send_bin(client, data, len, timeout);
}

/**
 * @brief Default platform hook to register a WebSocket event handler.
 *
 * @param client Client handle.
 * @param event Event identifier to subscribe.
 * @param handler Callback invoked on event.
 * @param handler_args Context pointer passed to the callback.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t register_events_default(esp_websocket_client_handle_t client, esp_websocket_event_id_t event,
                                         esp_event_handler_t handler, void *handler_args)
{
    return esp_websocket_register_events(client, event, handler, handler_args);
}

/**
 * @brief Default platform hook to create a FreeRTOS timer.
 *
 * @param name Timer name.
 * @param period Timer period in ticks.
 * @param auto_reload Auto-reload flag.
 * @param timer_id User context pointer.
 * @param callback Function invoked when the timer expires.
 * @return Handle to the created timer or NULL on failure.
 */
static TimerHandle_t timer_create_default(const char *name, TickType_t period, UBaseType_t auto_reload,
                                         void *timer_id, TimerCallbackFunction_t callback)
{
    return xTimerCreate(name, period, auto_reload, timer_id, callback);
}

/**
 * @brief Default platform hook to start a FreeRTOS timer.
 *
 * @param timer Timer handle.
 * @param ticks_to_wait Maximum block time to start the timer.
 * @return pdPASS on success or pdFAIL on error.
 */
static BaseType_t timer_start_default(TimerHandle_t timer, TickType_t ticks_to_wait)
{
    return xTimerStart(timer, ticks_to_wait);
}

/**
 * @brief Default platform hook to stop a FreeRTOS timer.
 *
 * @param timer Timer handle.
 * @param ticks_to_wait Maximum block time to stop the timer.
 * @return pdPASS on success or pdFAIL on error.
 */
static BaseType_t timer_stop_default(TimerHandle_t timer, TickType_t ticks_to_wait)
{
    return xTimerStop(timer, ticks_to_wait);
}

/**
 * @brief Default platform hook to delete a FreeRTOS timer.
 *
 * @param timer Timer handle.
 * @param ticks_to_wait Maximum block time to delete the timer.
 * @return pdPASS on success or pdFAIL on error.
 */
static BaseType_t timer_delete_default(TimerHandle_t timer, TickType_t ticks_to_wait)
{
    return xTimerDelete(timer, ticks_to_wait);
}

/**
 * @brief Default platform hook to adjust a FreeRTOS timer period.
 *
 * @param timer Timer handle.
 * @param new_period Updated period in ticks.
 * @param ticks_to_wait Maximum block time to apply the change.
 * @return pdPASS on success or pdFAIL on error.
 */
static BaseType_t timer_change_period_default(TimerHandle_t timer, TickType_t new_period, TickType_t ticks_to_wait)
{
    return xTimerChangePeriod(timer, new_period, ticks_to_wait);
}

static const ws_client_platform_t s_default_platform = {
    .client_init = client_init_default,
    .client_start = client_start_default,
    .client_stop = client_stop_default,
    .client_destroy = client_destroy_default,
    .client_get_uri = client_get_uri_default,
    .client_send_bin = client_send_bin_default,
    .register_events = register_events_default,
    .timer_create = timer_create_default,
    .timer_start = timer_start_default,
    .timer_stop = timer_stop_default,
    .timer_delete = timer_delete_default,
    .timer_change_period = timer_change_period_default,
};

static const ws_client_platform_t *s_platform = &s_default_platform;

/**
 * @brief Handle WebSocket client events and manage reconnection logic.
 *
 * @param handler_args User context (unused).
 * @param base Event base identifier.
 * @param event_id Event identifier.
 * @param event_data Pointer to event-specific data.
 * @return void
 */
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                                    void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        s_reconnect_delay_ms = s_reconnect_min_ms;
        ESP_LOGI(TAG, "Connected to %s", s_platform->client_get_uri(s_client));
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "WebSocket disconnected");
        if (s_should_run && s_reconnect_timer) {
            uint32_t delay = s_reconnect_delay_ms;
            if (delay > s_reconnect_max_ms) {
                delay = s_reconnect_max_ms;
            }
            s_platform->timer_change_period(s_reconnect_timer, pdMS_TO_TICKS(delay), 0);
            s_platform->timer_start(s_reconnect_timer, 0);
            if (s_reconnect_delay_ms < s_reconnect_max_ms) {
                uint64_t next = (uint64_t)s_reconnect_delay_ms * 2U;
                s_reconnect_delay_ms = (uint32_t)(next > s_reconnect_max_ms ? s_reconnect_max_ms : next);
            }
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        if (s_rx_cb && data->payload_len > 0) {
            uint32_t crc32 = 0;
            const uint8_t *payload = (const uint8_t *)data->data_ptr;
            size_t len = data->payload_len;
            if (data->op_code == WS_TRANSPORT_OPCODES_BINARY && len >= sizeof(uint32_t)) {
                crc32 = ((const uint32_t *)payload)[0];
                payload += sizeof(uint32_t);
                len -= sizeof(uint32_t);
            }
            s_rx_cb(payload, len, crc32, s_rx_ctx);
        }
        break;
    default:
        break;
    }
}

/**
 * @brief Timer callback that restarts the WebSocket connection.
 *
 * @param timer Timer handle invoking the callback.
 * @return void
 */
static void reconnect_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    if (s_client && s_should_run) {
        ESP_LOGI(TAG, "Reconnecting WebSocket");
        s_platform->client_start(s_client);
    }
}

/**
 * @brief Stop and destroy the active WebSocket client and timers.
 *
 * @return void
 */
static void cleanup_client(void)
{
    if (s_reconnect_timer) {
        s_platform->timer_stop(s_reconnect_timer, portMAX_DELAY);
        s_platform->timer_delete(s_reconnect_timer, portMAX_DELAY);
        s_reconnect_timer = NULL;
    }
    if (s_client) {
        s_platform->client_stop(s_client);
        s_platform->client_destroy(s_client);
        s_client = NULL;
    }
    free(s_auth_header);
    s_auth_header = NULL;
    s_rx_cb = NULL;
    s_rx_ctx = NULL;
    s_connected = false;
}

/**
 * @brief Initialize and start the WebSocket client with reconnection support.
 *
 * @param config Pointer to the client configuration structure.
 * @param cb Receive callback invoked for incoming payloads.
 * @param ctx User context passed to the receive callback.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
esp_err_t ws_client_start(const ws_client_config_t *config, ws_client_rx_cb_t cb, void *ctx)
{
    if (!config || !config->uri) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_client) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_websocket_client_config_t ws_cfg = {
        .uri = config->uri,
        .cert_pem = (const char *)config->ca_cert,
        .cert_len = config->ca_cert_len,
        .skip_cert_common_name_check = config->skip_common_name_check,
    };

    if (config->auth_token) {
        static const char prefix[] = "Authorization: Bearer ";
        const size_t token_len = strlen(config->auth_token);
        const size_t header_len = sizeof(prefix) - 1U + token_len + 2U; // CRLF
        s_auth_header = (char *)malloc(header_len + 1U);
        if (!s_auth_header) {
            return ESP_ERR_NO_MEM;
        }
        int written = snprintf(s_auth_header, header_len + 1U, "%s%s\r\n", prefix, config->auth_token);
        if (written < 0 || (size_t)written >= header_len + 1U) {
            free(s_auth_header);
            s_auth_header = NULL;
            return ESP_ERR_INVALID_SIZE;
        }
        ws_cfg.headers = s_auth_header;
    }

    s_client = s_platform->client_init(&ws_cfg);
    if (!s_client) {
        free(s_auth_header);
        s_auth_header = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_reconnect_min_ms = config->reconnect_min_delay_ms ? config->reconnect_min_delay_ms : 2000;
    s_reconnect_max_ms = config->reconnect_max_delay_ms ? config->reconnect_max_delay_ms : 60000;
    if (s_reconnect_min_ms > s_reconnect_max_ms) {
        s_reconnect_max_ms = s_reconnect_min_ms;
    }
    s_reconnect_delay_ms = s_reconnect_min_ms;

    s_reconnect_timer = s_platform->timer_create("ws_retry", pdMS_TO_TICKS(s_reconnect_min_ms), pdFALSE, NULL,
                                                reconnect_timer_cb);
    if (!s_reconnect_timer) {
        cleanup_client();
        return ESP_ERR_NO_MEM;
    }

    s_rx_cb = cb;
    s_rx_ctx = ctx;
    s_should_run = true;
    s_connected = false;

    s_platform->register_events(s_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);

    esp_err_t err = s_platform->client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket start failed: %s", esp_err_to_name(err));
        cleanup_client();
        return err;
    }
    return ESP_OK;
}


/**
 * @brief Stop the WebSocket client and release resources.
 *
 * @return void
 */
void ws_client_stop(void)
{
    s_should_run = false;
    cleanup_client();
}

/**
 * @brief Send a binary payload through the active WebSocket connection.
 *
 * @param data Pointer to the payload buffer.
 * @param len Payload size in bytes.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
esp_err_t ws_client_send(const uint8_t *data, size_t len)
{
    if (!s_client || !s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    int sent = s_platform->client_send_bin(s_client, (const char *)data, len, portMAX_DELAY);
    return sent >= 0 ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Retrieve the current WebSocket connection status.
 *
 * @return true when connected to the server, false otherwise.
 */
bool ws_client_is_connected(void)
{
    return s_connected;
}

/**
 * @brief Override the WebSocket client platform hooks.
 *
 * @param platform Pointer to the platform definition (NULL for defaults).
 * @return void
 */
void ws_client_set_platform(const ws_client_platform_t *platform)
{
    s_platform = platform ? platform : &s_default_platform;
}
