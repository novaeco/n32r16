#include "ws_client_core.h"

#include "esp_log.h"
#include "esp_websocket_client.h"

static const char *TAG = "ws_client";
static esp_websocket_client_handle_t s_client;
static ws_client_rx_cb_t s_rx_cb;

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                                    void *event_data) {
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket disconnected");
            break;
        case WEBSOCKET_EVENT_DATA: {
            if (s_rx_cb != NULL && data->data_ptr != NULL && data->data_len > 0) {
                bool is_text = (data->op_code == WS_TRANSPORT_OPCODES_TEXT || data->op_code == WS_TRANSPORT_OPCODES_CONT);
                s_rx_cb((const uint8_t *)data->data_ptr, data->data_len, is_text);
            }
            break;
        }
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            break;
        default:
            break;
    }
}

esp_err_t ws_client_start(const ws_client_config_t *cfg) {
    if (cfg == NULL || cfg->uri == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_rx_cb = cfg->on_rx;

    esp_websocket_client_config_t client_config = {
        .uri = cfg->uri,
        .disable_auto_reconnect = false,
    };

    s_client = esp_websocket_client_init(&client_config);
    if (s_client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
    if (err != ESP_OK) {
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        return err;
    }

    return esp_websocket_client_start(s_client);
}

esp_err_t ws_client_send(const uint8_t *data, size_t len, bool is_text) {
    if (s_client == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    int sent = 0;
    if (is_text) {
        sent = esp_websocket_client_send_text(s_client, (const char *)data, len, pdMS_TO_TICKS(1000));
    } else {
        sent = esp_websocket_client_send_bin(s_client, (const char *)data, len, pdMS_TO_TICKS(1000));
    }
    return sent >= 0 ? ESP_OK : ESP_FAIL;
}

void ws_client_stop(void) {
    if (s_client != NULL) {
        esp_websocket_client_close(s_client, pdMS_TO_TICKS(1000));
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
}

