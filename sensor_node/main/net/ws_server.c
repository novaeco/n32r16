#include "net/ws_server.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "common/net/ws_server_core.h"
#include "data_model.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "tasks/t_io.h"
#include "time_sync.h"

extern const uint8_t sensor_ws_server_crt_start[] asm("_binary_sensor_ws_server_crt_start");
extern const uint8_t sensor_ws_server_crt_end[] asm("_binary_sensor_ws_server_crt_end");
extern const uint8_t sensor_ws_server_key_start[] asm("_binary_sensor_ws_server_key_start");
extern const uint8_t sensor_ws_server_key_end[] asm("_binary_sensor_ws_server_key_end");

#define SENSOR_WS_BUFFER_SIZE 2048

static const char *TAG = "sensor_ws";
static SemaphoreHandle_t s_send_mutex;
static uint8_t s_last_snapshot[SENSOR_WS_BUFFER_SIZE];
static size_t s_last_snapshot_len;
static bool s_last_snapshot_valid;

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
        ESP_LOGW(TAG, "Invalid command payload");
    }
}

static void handle_client_event(int fd, ws_server_client_event_t event) {
    (void)fd;
    if (event == WS_SERVER_CLIENT_CONNECTED) {
        update_ws_state(NETWORK_STATE_READY);
        if (s_last_snapshot_valid) {
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

esp_err_t sensor_ws_server_start(void) {
    s_send_mutex = xSemaphoreCreateMutex();
    if (s_send_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    s_last_snapshot_len = 0;
    s_last_snapshot_valid = false;

    update_ws_state(NETWORK_STATE_CONNECTING);

    ws_server_config_t cfg = {
        .port = 8443,
        .on_rx = handle_rx,
        .on_client_event = handle_client_event,
        .server_cert = sensor_ws_server_crt_start,
        .server_cert_len = (size_t)(sensor_ws_server_crt_end - sensor_ws_server_crt_start),
        .private_key = sensor_ws_server_key_start,
        .private_key_len = (size_t)(sensor_ws_server_key_end - sensor_ws_server_key_start),
    };
    return ws_server_start(&cfg);
}

void sensor_ws_server_send_snapshot(void) {
    if (s_send_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(s_send_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    char buffer[SENSOR_WS_BUFFER_SIZE];
    size_t len = 0;
    data_model_set_time_synchronized(time_sync_is_synchronized());
    if (!data_model_format_sensor_update(buffer, sizeof(buffer), &len, NULL)) {
        ESP_LOGE(TAG, "Failed to serialize snapshot");
        update_ws_state(NETWORK_STATE_ERROR);
        xSemaphoreGive(s_send_mutex);
        return;
    }

    esp_err_t err = ws_server_send((const uint8_t *)buffer, len);
    if (err == ESP_OK) {
        memcpy(s_last_snapshot, buffer, len);
        s_last_snapshot_len = len;
        s_last_snapshot_valid = true;
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

