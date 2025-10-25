#include "ws_client.h"

#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "ws_security.h"
#include "base64_utils.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ws_client";

static esp_websocket_client_handle_t s_client;
static ws_client_rx_cb_t s_rx_cb;
static void *s_rx_ctx;
static ws_client_error_cb_t s_error_cb;
static void *s_error_ctx;
static bool s_connected;
static bool s_should_run;
static TimerHandle_t s_reconnect_timer;
static uint32_t s_reconnect_delay_ms;
static uint32_t s_reconnect_min_ms;
static uint32_t s_reconnect_max_ms;
static char *s_header_block;
static ws_security_context_t s_security_ctx;
static uint64_t s_rx_counter;
static const char *s_token_ref;
static size_t s_header_len;
static bool s_handshake_enabled;

static void bytes_to_hex(const uint8_t *in, size_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[2U * i] = hex[in[i] >> 4U];
        out[2U * i + 1U] = hex[in[i] & 0x0FU];
    }
    out[2U * len] = '\0';
}

static esp_err_t regenerate_headers(void)
{
    if (s_header_len == 0) {
        return ESP_OK;
    }
    if (!s_header_block) {
        s_header_block = (char *)malloc(s_header_len + 1U);
        if (!s_header_block) {
            return ESP_ERR_NO_MEM;
        }
    }
    size_t offset = 0;
    const char *token = s_token_ref ? s_token_ref : "";
    if (token[0] != '\0') {
        int written = snprintf(s_header_block + offset, s_header_len - offset + 1U, "Authorization: Bearer %s\r\n",
                               token);
        if (written < 0 || (size_t)written > s_header_len - offset) {
            return ESP_ERR_INVALID_SIZE;
        }
        offset += (size_t)written;
    }
    if (s_handshake_enabled) {
        uint8_t nonce[WS_SECURITY_NONCE_LEN];
        char nonce_hex[WS_SECURITY_NONCE_LEN * 2U + 1U];
        uint8_t signature[32];
        char signature_b64[96];
        size_t signature_b64_len = 0;
        esp_fill_random(nonce, sizeof(nonce));
        bytes_to_hex(nonce, sizeof(nonce), nonce_hex);
        esp_err_t sig_err = ws_security_compute_handshake_signature(&s_security_ctx, nonce, sizeof(nonce), token,
                                                                    signature, sizeof(signature));
        if (sig_err != ESP_OK) {
            return sig_err;
        }
        esp_err_t enc_err = base64_utils_encode(signature, sizeof(signature), signature_b64, sizeof(signature_b64),
                                                &signature_b64_len);
        if (enc_err != ESP_OK) {
            return enc_err;
        }
        int written = snprintf(s_header_block + offset, s_header_len - offset + 1U, "X-WS-Nonce: %s\r\n", nonce_hex);
        if (written < 0 || (size_t)written > s_header_len - offset) {
            return ESP_ERR_INVALID_SIZE;
        }
        offset += (size_t)written;
        written = snprintf(s_header_block + offset, s_header_len - offset + 1U, "X-WS-Signature: %s\r\n",
                           signature_b64);
        if (written < 0 || (size_t)written > s_header_len - offset) {
            return ESP_ERR_INVALID_SIZE;
        }
        offset += (size_t)written;
    }
    if (offset <= s_header_len) {
        s_header_block[offset] = '\0';
    }
    return ESP_OK;
}

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
        ws_security_reset_counters(&s_security_ctx);
        s_rx_counter = 0;
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
            uint8_t *payload = (uint8_t *)data->data_ptr;
            size_t len = data->payload_len;
            if (ws_security_is_encryption_enabled(&s_security_ctx) && data->op_code == WS_TRANSPORT_OPCODES_BINARY) {
                size_t plaintext_len = 0;
                esp_err_t dec_err = ws_security_decrypt(&s_security_ctx, payload, len, &plaintext_len, &s_rx_counter);
                if (dec_err != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to decrypt frame: %s", esp_err_to_name(dec_err));
                    break;
                }
                len = plaintext_len;
            }
            if (data->op_code == WS_TRANSPORT_OPCODES_BINARY && len >= sizeof(uint32_t)) {
                crc32 = ((const uint32_t *)payload)[0];
                payload += sizeof(uint32_t);
                len -= sizeof(uint32_t);
            }
            s_rx_cb(payload, len, crc32, s_rx_ctx);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        if (s_error_cb && data) {
            s_error_cb(data, s_error_ctx);
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
        if (s_header_len > 0U) {
            esp_err_t err = regenerate_headers();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to rebuild handshake headers: %s", esp_err_to_name(err));
                return;
            }
        }
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
    free(s_header_block);
    s_header_block = NULL;
    s_rx_cb = NULL;
    s_rx_ctx = NULL;
    s_error_cb = NULL;
    s_error_ctx = NULL;
    s_connected = false;
    s_rx_counter = 0;
    memset(&s_security_ctx, 0, sizeof(s_security_ctx));
    s_token_ref = NULL;
    s_header_len = 0;
    s_handshake_enabled = false;
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

    ws_security_config_t sec_cfg = {
        .secret = config->crypto_secret,
        .secret_len = config->crypto_secret_len,
        .enable_encryption = config->enable_frame_encryption,
        .enable_handshake = config->enable_handshake_token,
    };
    esp_err_t sec_err = ws_security_context_init(&s_security_ctx, &sec_cfg);
    if (sec_err != ESP_OK) {
        return sec_err;
    }
    ws_security_reset_counters(&s_security_ctx);
    s_rx_counter = 0;

    esp_websocket_client_config_t ws_cfg = {
        .uri = config->uri,
        .cert_pem = (const char *)config->ca_cert,
        .cert_len = config->ca_cert_len,
        .skip_cert_common_name_check = config->skip_common_name_check,
    };

    if (config->tls_server_name && config->tls_server_name[0] != '\0') {
        ws_cfg.host = config->tls_server_name;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
        ws_cfg.common_name = config->tls_server_name;
#endif
    }

    const char *token = config->auth_token ? config->auth_token : "";
    const bool have_token = token[0] != '\0';
    s_handshake_enabled = ws_security_is_handshake_enabled(&s_security_ctx);
    size_t header_len = 0;
    if (have_token) {
        header_len += strlen("Authorization: Bearer ") + strlen(token) + 2U;
    }
    if (s_handshake_enabled) {
        header_len += strlen("X-WS-Nonce: ") + (WS_SECURITY_NONCE_LEN * 2U) + 2U;
        header_len += strlen("X-WS-Signature: ") + 44U + 2U;
    }
    s_token_ref = token;
    s_header_len = header_len;
    if (header_len > 0U) {
        esp_err_t hdr_err = regenerate_headers();
        if (hdr_err != ESP_OK) {
            free(s_header_block);
            s_header_block = NULL;
            s_header_len = 0;
            return hdr_err;
        }
        ws_cfg.headers = s_header_block;
    }

    s_client = s_platform->client_init(&ws_cfg);
    if (!s_client) {
        free(s_header_block);
        s_header_block = NULL;
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
    s_error_cb = config->error_cb;
    s_error_ctx = config->error_ctx;
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
    const uint8_t *frame_data = data;
    size_t frame_len = len;
    uint8_t *encrypted = NULL;
    if (ws_security_is_encryption_enabled(&s_security_ctx)) {
        size_t required = ws_security_encrypted_size(&s_security_ctx, len);
        encrypted = (uint8_t *)malloc(required);
        if (!encrypted) {
            return ESP_ERR_NO_MEM;
        }
        esp_err_t enc_err = ws_security_encrypt(&s_security_ctx, data, len, encrypted, required, &frame_len);
        if (enc_err != ESP_OK) {
            free(encrypted);
            return enc_err;
        }
        frame_data = encrypted;
    }
    int sent = s_platform->client_send_bin(s_client, (const char *)frame_data, frame_len, portMAX_DELAY);
    free(encrypted);
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
