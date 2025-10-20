#include "net/ws_client.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "common/net/mdns_support.h"
#include "common/net/ws_client_core.h"
#include "data_model.h"
#include "esp_check.h"
#include "esp_log.h"

extern const uint8_t sensor_ws_server_crt_start[] asm("_binary_sensor_ws_server_crt_start");

static const char *TAG = "hmi_ws";
static ws_update_cb_t s_update_cb;
static char s_ws_uri[128];
static uint32_t s_retry_count;

static void ws_rx_handler(const uint8_t *data, size_t len) {
    if (hmi_data_model_apply_update(data, len)) {
        if (s_update_cb != NULL) {
            s_update_cb();
        }
    } else {
        ESP_LOGW(TAG, "Failed to parse sensor update");
    }
}

static void ws_event_handler(ws_client_event_t event, esp_err_t reason) {
    (void)reason;
    switch (event) {
        case WS_CLIENT_EVENT_CONNECTING:
            hmi_data_model_set_ws_connected(false);
            break;
        case WS_CLIENT_EVENT_CONNECTED:
            s_retry_count = 0;
            hmi_data_model_set_ws_connected(true);
            hmi_data_model_set_ws_retries(s_retry_count);
            break;
        case WS_CLIENT_EVENT_DISCONNECTED:
            hmi_data_model_set_ws_connected(false);
            break;
        case WS_CLIENT_EVENT_ERROR:
            if (s_retry_count < UINT32_MAX) {
                s_retry_count++;
            }
            hmi_data_model_set_ws_retries(s_retry_count);
            hmi_data_model_set_ws_connected(false);
            break;
        default:
            break;
    }
}

esp_err_t hmi_ws_client_start(void) {
    char host[64];
    uint16_t port = 0;
    ESP_RETURN_ON_ERROR(mdns_find_service("_hmi-sensor", host, sizeof(host), &port), TAG,
                        "mDNS lookup failed");

    snprintf(s_ws_uri, sizeof(s_ws_uri), "wss://%s:%u/ws", host, port);
    ws_client_config_t cfg = {
        .uri = s_ws_uri,
        .cert_pem = (const char *)sensor_ws_server_crt_start,
        .on_rx = ws_rx_handler,
        .reconnect_min_ms = 2000,
        .reconnect_max_ms = 60000,
    };
    hmi_data_model_set_ws_retries(0);
    ws_client_register_event_callback(ws_event_handler);
    return ws_client_start(&cfg);
}

void hmi_ws_client_set_update_callback(ws_update_cb_t cb) {
    s_update_cb = cb;
}

esp_err_t hmi_ws_client_send(const char *payload) {
    if (payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return ws_client_send((const uint8_t *)payload, strlen(payload));
}

