#include "ws_client.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include <stdlib.h>

static const char *TAG = "ws_client";

static esp_websocket_client_handle_t s_client;
static ws_client_rx_cb_t s_rx_cb;
static void *s_rx_ctx;
static bool s_connected;

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                                    void *event_data)
{
    (void)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "Connected to %s", esp_websocket_client_get_uri(s_client));
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "Disconnected");
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

esp_err_t ws_client_start(const ws_client_config_t *config, ws_client_rx_cb_t cb, void *ctx)
{
    if (!config || !config->uri) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_client) {
        return ESP_OK;
    }

    esp_websocket_client_config_t ws_cfg = {
        .uri = config->uri,
        .transport = config->use_ssl ? WEBSOCKET_TRANSPORT_OVER_SSL : WEBSOCKET_TRANSPORT_OVER_TCP,
    };

    s_client = esp_websocket_client_init(&ws_cfg);
    if (!s_client) {
        return ESP_ERR_NO_MEM;
    }

    s_rx_cb = cb;
    s_rx_ctx = ctx;
    s_connected = false;

    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);

    esp_err_t err = esp_websocket_client_start(s_client);
    if (err != ESP_OK) {
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        return err;
    }
    return ESP_OK;
}

void ws_client_stop(void)
{
    if (s_client) {
        esp_websocket_client_stop(s_client);
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        s_connected = false;
    }
}

esp_err_t ws_client_send(const uint8_t *data, size_t len)
{
    if (!s_client || !s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    int sent = esp_websocket_client_send_bin(s_client, (const char *)data, len, portMAX_DELAY);
    return sent >= 0 ? ESP_OK : ESP_FAIL;
}

bool ws_client_is_connected(void)
{
    return s_connected;
}
