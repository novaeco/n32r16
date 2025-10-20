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

static void ws_rx_handler(const uint8_t *data, size_t len) {
    if (hmi_data_model_apply_update(data, len)) {
        if (s_update_cb != NULL) {
            s_update_cb();
        }
    } else {
        ESP_LOGW(TAG, "Failed to parse sensor update");
    }
}

esp_err_t hmi_ws_client_start(void) {
    char host[64];
    uint16_t port = 0;
    ESP_RETURN_ON_ERROR(mdns_find_service("_hmi-sensor", host, sizeof(host), &port), TAG,
                        "mDNS lookup failed");

    char uri[128];
    snprintf(uri, sizeof(uri), "ws://%s:%u/ws", host, port);
    ws_client_config_t cfg = {
        .uri = uri,
        .on_rx = ws_rx_handler,
    };
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

