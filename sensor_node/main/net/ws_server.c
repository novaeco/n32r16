#include "net/ws_server.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "common/net/tls_credentials.h"
#include "common/net/ws_server_core.h"
#include "data_model.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "tasks/t_io.h"
#include "time_sync.h"

#ifndef CONFIG_SENSOR_NODE_WS_MAX_FRAME_SIZE
#define CONFIG_SENSOR_NODE_WS_MAX_FRAME_SIZE 4096
#endif

static const char *TAG = "sensor_ws";
static SemaphoreHandle_t s_send_mutex;
static uint8_t *s_frame_buffer;
static uint8_t *s_last_snapshot;
static size_t s_frame_capacity;
static size_t s_last_snapshot_len;
static bool s_last_snapshot_valid;
static tls_server_credentials_t s_tls_creds;
static bool s_tls_ready;

static void update_ws_state(network_state_t state) {
    data_model_set_ws_state(state, (uint8_t)ws_server_active_client_count());
}

static void handle_rx(const uint8_t *data, size_t len) {
    io_command_t cmd;
    if (data_model_parse_command(data, len, &cmd)) {
        if (!t_io_post_command(&cmd)) {
            ESP_LOGW(TAG, "IO command queue full");
        }
    } else {
        ESP_LOGW(TAG, "Rejected command payload");
    }
}

static void handle_client_event(int fd, ws_server_client_event_t event) {
    (void)fd;
    if (event == WS_SERVER_CLIENT_CONNECTED) {
        update_ws_state(NETWORK_STATE_READY);
        if (s_last_snapshot_valid && s_last_snapshot != NULL) {
            esp_err_t err = ws_server_send_to(fd, s_last_snapshot, s_last_snapshot_len);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to replay snapshot to new client: %s", esp_err_to_name(err));
            }
        }
    } else {
        if (ws_server_active_client_count() == 0) {
            update_ws_state(NETWORK_STATE_CONNECTING);
        } else {
            update_ws_state(NETWORK_STATE_READY);
        }
    }
}

static void release_resources(void) {
    if (s_send_mutex != NULL) {
        vSemaphoreDelete(s_send_mutex);
        s_send_mutex = NULL;
    }
    if (s_frame_buffer != NULL) {
        heap_caps_free(s_frame_buffer);
        s_frame_buffer = NULL;
    }
    if (s_last_snapshot != NULL) {
        heap_caps_free(s_last_snapshot);
        s_last_snapshot = NULL;
    }
    s_last_snapshot_valid = false;
    s_last_snapshot_len = 0;
    s_frame_capacity = 0;
    if (s_tls_ready) {
        tls_credentials_release(&s_tls_creds);
        s_tls_ready = false;
    }
}

esp_err_t sensor_ws_server_start(void) {
    release_resources();

    s_send_mutex = xSemaphoreCreateMutex();
    if (s_send_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_frame_capacity = CONFIG_SENSOR_NODE_WS_MAX_FRAME_SIZE;
    s_frame_buffer = heap_caps_malloc(s_frame_capacity, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    s_last_snapshot = heap_caps_malloc(s_frame_capacity, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (s_frame_buffer == NULL || s_last_snapshot == NULL) {
        ESP_LOGE(TAG, "Failed to allocate WS buffers");
        release_resources();
        return ESP_ERR_NO_MEM;
    }
    s_last_snapshot_len = 0;
    s_last_snapshot_valid = false;

    esp_err_t err = tls_credentials_load_server(&s_tls_creds);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TLS credential load failed: %s", esp_err_to_name(err));
        release_resources();
        return err;
    }
    s_tls_ready = true;

    update_ws_state(NETWORK_STATE_CONNECTING);

    ws_server_config_t cfg = {
        .port = 8443,
        .on_rx = handle_rx,
        .on_client_event = handle_client_event,
        .server_cert = s_tls_creds.certificate,
        .server_cert_len = s_tls_creds.certificate_len,
        .private_key = s_tls_creds.private_key,
        .private_key_len = s_tls_creds.private_key_len,
    };

    err = ws_server_start(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WS server start failed: %s", esp_err_to_name(err));
        release_resources();
        return err;
    }
    return ESP_OK;
}

void sensor_ws_server_send_snapshot(void) {
    if (s_send_mutex == NULL || s_frame_buffer == NULL) {
        return;
    }
    if (xSemaphoreTake(s_send_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    size_t len = 0;
    data_model_set_time_synchronized(time_sync_is_synchronized());
    if (!data_model_format_sensor_update((char *)s_frame_buffer, s_frame_capacity, &len, NULL)) {
        ESP_LOGE(TAG, "Failed to serialize snapshot");
        update_ws_state(NETWORK_STATE_ERROR);
        xSemaphoreGive(s_send_mutex);
        return;
    }

    esp_err_t err = ws_server_send((const uint8_t *)s_frame_buffer, len);
    if (err == ESP_OK) {
        if (len <= s_frame_capacity) {
            memcpy(s_last_snapshot, s_frame_buffer, len);
            s_last_snapshot_len = len;
            s_last_snapshot_valid = true;
        } else {
            ESP_LOGW(TAG, "Snapshot truncated: len=%zu", len);
            s_last_snapshot_valid = false;
        }
        update_ws_state(NETWORK_STATE_READY);
    } else {
        ESP_LOGW(TAG, "Broadcast failed: %s", esp_err_to_name(err));
        if (err == ESP_ERR_INVALID_STATE) {
            update_ws_state(NETWORK_STATE_CONNECTING);
        } else {
            update_ws_state(NETWORK_STATE_ERROR);
        }
    }
    xSemaphoreGive(s_send_mutex);
}

