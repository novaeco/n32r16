#include "ws_client_core.h"

#include <stdlib.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "ws_client";

#define WS_EVENT_CONNECTED BIT0
#define WS_EVENT_RECONNECT BIT1
#define WS_EVENT_STOP BIT2

typedef struct {
    char *uri;
    char *cert_pem;
    ws_client_rx_cb_t on_rx;
    uint32_t reconnect_min_ms;
    uint32_t reconnect_max_ms;
} ws_client_runtime_cfg_t;

static struct {
    esp_websocket_client_handle_t client;
    ws_client_runtime_cfg_t cfg;
    ws_client_event_cb_t event_cb;
    EventGroupHandle_t events;
    TaskHandle_t worker;
    bool running;
    esp_err_t last_error;
} s_ctx;

static void ws_client_invoke_event(ws_client_event_t evt, esp_err_t reason) {
    if (s_ctx.event_cb != NULL) {
        s_ctx.event_cb(evt, reason);
    }
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id,
                                    void *event_data) {
    (void)handler_args;
    (void)base;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket connected");
            s_ctx.last_error = ESP_OK;
            xEventGroupSetBits(s_ctx.events, WS_EVENT_CONNECTED);
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket disconnected");
            s_ctx.last_error = ESP_OK;
            if (data != NULL && data->error_handle != NULL) {
                if (data->error_handle->error_type != WEBSOCKET_ERROR_TYPE_NONE) {
                    s_ctx.last_error = ESP_FAIL;
                }
            }
            xEventGroupSetBits(s_ctx.events, WS_EVENT_RECONNECT);
            break;
        case WEBSOCKET_EVENT_DATA:
            if (s_ctx.cfg.on_rx != NULL && data != NULL && data->data_ptr != NULL &&
                data->data_len > 0) {
                s_ctx.cfg.on_rx((const uint8_t *)data->data_ptr, data->data_len);
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error");
            s_ctx.last_error = ESP_FAIL;
            xEventGroupSetBits(s_ctx.events, WS_EVENT_RECONNECT);
            break;
        default:
            break;
    }
}

static void ws_client_worker(void *arg) {
    (void)arg;
    uint32_t backoff = s_ctx.cfg.reconnect_min_ms;
    while (true) {
        if ((xEventGroupGetBits(s_ctx.events) & WS_EVENT_STOP) != 0U) {
            break;
        }

        ws_client_invoke_event(WS_CLIENT_EVENT_CONNECTING, ESP_OK);

        esp_err_t err = esp_websocket_client_start(s_ctx.client);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "WebSocket start failed: %s", esp_err_to_name(err));
            ws_client_invoke_event(WS_CLIENT_EVENT_ERROR, err);
        } else {
            EventBits_t bits = xEventGroupWaitBits(s_ctx.events, WS_EVENT_CONNECTED | WS_EVENT_STOP,
                                                   pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
            if (bits & WS_EVENT_STOP) {
                break;
            }
            if (bits & WS_EVENT_CONNECTED) {
                ws_client_invoke_event(WS_CLIENT_EVENT_CONNECTED, ESP_OK);
                backoff = s_ctx.cfg.reconnect_min_ms;
                EventBits_t wait_bits = xEventGroupWaitBits(s_ctx.events,
                                                            WS_EVENT_RECONNECT | WS_EVENT_STOP,
                                                            pdTRUE, pdFALSE, portMAX_DELAY);
                if (wait_bits & WS_EVENT_STOP) {
                    break;
                }
                ws_client_invoke_event(WS_CLIENT_EVENT_DISCONNECTED, s_ctx.last_error);
                esp_websocket_client_stop(s_ctx.client);
            } else {
                ESP_LOGW(TAG, "WebSocket connection timeout");
                ws_client_invoke_event(WS_CLIENT_EVENT_ERROR, ESP_ERR_TIMEOUT);
                esp_websocket_client_stop(s_ctx.client);
            }
        }

        if ((xEventGroupGetBits(s_ctx.events) & WS_EVENT_STOP) != 0U) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(backoff));
        if (backoff < s_ctx.cfg.reconnect_max_ms) {
            uint64_t next = (uint64_t)backoff * 2ULL;
            if (next > s_ctx.cfg.reconnect_max_ms) {
                next = s_ctx.cfg.reconnect_max_ms;
            }
            backoff = (uint32_t)next;
        }
        xEventGroupClearBits(s_ctx.events, WS_EVENT_CONNECTED | WS_EVENT_RECONNECT);
    }

    s_ctx.running = false;
    s_ctx.worker = NULL;
    vTaskDelete(NULL);
}

esp_err_t ws_client_start(const ws_client_config_t *cfg) {
    if (cfg == NULL || cfg->uri == NULL || cfg->on_rx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ctx.running) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.last_error = ESP_OK;
    s_ctx.cfg.uri = strdup(cfg->uri);
    if (s_ctx.cfg.uri == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (cfg->cert_pem != NULL) {
        s_ctx.cfg.cert_pem = strdup(cfg->cert_pem);
        if (s_ctx.cfg.cert_pem == NULL) {
            free(s_ctx.cfg.uri);
            s_ctx.cfg.uri = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    s_ctx.cfg.on_rx = cfg->on_rx;
    s_ctx.cfg.reconnect_min_ms = cfg->reconnect_min_ms == 0 ? 1000 : cfg->reconnect_min_ms;
    s_ctx.cfg.reconnect_max_ms = cfg->reconnect_max_ms == 0 ? 60000 : cfg->reconnect_max_ms;
    if (s_ctx.cfg.reconnect_max_ms < s_ctx.cfg.reconnect_min_ms) {
        s_ctx.cfg.reconnect_max_ms = s_ctx.cfg.reconnect_min_ms;
    }

    s_ctx.events = xEventGroupCreate();
    if (s_ctx.events == NULL) {
        free(s_ctx.cfg.uri);
        free(s_ctx.cfg.cert_pem);
        return ESP_ERR_NO_MEM;
    }

    esp_websocket_client_config_t client_config = {
        .uri = s_ctx.cfg.uri,
        .cert_pem = s_ctx.cfg.cert_pem,
        .disable_auto_reconnect = true,
        .keep_alive_enable = true,
        .keep_alive_idle = 5,
        .keep_alive_interval = 5,
        .keep_alive_count = 3,
    };

    s_ctx.client = esp_websocket_client_init(&client_config);
    if (s_ctx.client == NULL) {
        vEventGroupDelete(s_ctx.events);
        s_ctx.events = NULL;
        free(s_ctx.cfg.uri);
        free(s_ctx.cfg.cert_pem);
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_websocket_register_events(s_ctx.client, WEBSOCKET_EVENT_ANY,
                                                  websocket_event_handler, NULL));

    s_ctx.running = true;
    BaseType_t ret = xTaskCreate(ws_client_worker, "ws_client", 4096, NULL, 5, &s_ctx.worker);
    if (ret != pdPASS) {
        s_ctx.running = false;
        esp_websocket_client_destroy(s_ctx.client);
        s_ctx.client = NULL;
        vEventGroupDelete(s_ctx.events);
        s_ctx.events = NULL;
        free(s_ctx.cfg.uri);
        free(s_ctx.cfg.cert_pem);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t ws_client_send(const uint8_t *data, size_t len) {
    if (!s_ctx.running || s_ctx.client == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    int sent = esp_websocket_client_send_text(s_ctx.client, (const char *)data, len, pdMS_TO_TICKS(1000));
    return sent >= 0 ? ESP_OK : ESP_FAIL;
}

void ws_client_register_event_callback(ws_client_event_cb_t cb) {
    s_ctx.event_cb = cb;
}

void ws_client_stop(void) {
    if (!s_ctx.running) {
        return;
    }
    xEventGroupSetBits(s_ctx.events, WS_EVENT_STOP);
    if (s_ctx.client != NULL) {
        esp_websocket_client_close(s_ctx.client, pdMS_TO_TICKS(1000));
    }
    while (s_ctx.running) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_ctx.client != NULL) {
        esp_websocket_client_destroy(s_ctx.client);
        s_ctx.client = NULL;
    }
    if (s_ctx.events != NULL) {
        vEventGroupDelete(s_ctx.events);
        s_ctx.events = NULL;
    }
    free(s_ctx.cfg.uri);
    free(s_ctx.cfg.cert_pem);
    memset(&s_ctx, 0, sizeof(s_ctx));
}

