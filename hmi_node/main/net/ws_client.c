#include "net/ws_client.h"

#include <stdio.h>
#include <string.h>

#include "common/net/mdns_support.h"
#include "common/net/ws_client_core.h"
#include "data_model.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "hmi_ws";
static ws_update_cb_t s_update_cb;
static char s_cached_uri[128];
static bool s_client_started;
static bool s_force_redetect;

static void ws_rx_handler(const uint8_t *data, size_t len, bool is_text) {
    (void)is_text;
    if (hmi_data_model_ingest(data, len)) {
        if (s_update_cb != NULL) {
            s_update_cb();
        }
    } else {
        ESP_LOGW(TAG, "Failed to parse incoming payload");
    }
}

static esp_err_t ensure_uri(void) {
    if (s_cached_uri[0] != '\0' && !s_force_redetect) {
        return ESP_OK;
    }
    char host[64] = {0};
    uint16_t port = 0;
    ESP_RETURN_ON_ERROR(mdns_find_service("_hmi-sensor", host, sizeof(host), &port), TAG,
                        "mDNS lookup failed");
    snprintf(s_cached_uri, sizeof(s_cached_uri), "ws://%s:%u/ws", host, port);
    s_force_redetect = false;
    return ESP_OK;
}

esp_err_t hmi_ws_client_start(void) {
    ESP_RETURN_ON_ERROR(ensure_uri(), TAG, "URI discovery failed");

    ws_client_config_t cfg = {
        .uri = s_cached_uri,
        .on_rx = ws_rx_handler,
    };
    esp_err_t err = ws_client_start(&cfg);
    if (err == ESP_OK) {
        s_client_started = true;
    }
    return err;
}

void hmi_ws_client_set_update_callback(ws_update_cb_t cb) {
    s_update_cb = cb;
}

esp_err_t hmi_ws_client_send(const proto_buffer_t *buffer) {
    if (buffer == NULL || buffer->data == NULL || buffer->len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return ws_client_send(buffer->data, buffer->len, buffer->is_text);
}

void hmi_ws_client_request_reconnect(void) {
    if (s_client_started) {
        ws_client_stop();
        s_client_started = false;
    }
    s_force_redetect = true;
}

