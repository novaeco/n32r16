#include "net/ws_server.h"

#include <stdlib.h>
#include <string.h>

#include "common/net/ws_server_core.h"
#include "data_model.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "tasks/t_io.h"

static const char *TAG = "sensor_ws";
static SemaphoreHandle_t s_send_mutex;

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

esp_err_t sensor_ws_server_start(void) {
    s_send_mutex = xSemaphoreCreateMutex();
    if (s_send_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ws_server_config_t cfg = {
        .port = 8080,
        .on_rx = handle_rx,
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
    uint32_t crc = 0;
    char *json = data_model_create_sensor_update(&crc);
    if (json != NULL) {
        ws_server_send((const uint8_t *)json, strlen(json));
        free(json);
    }
    xSemaphoreGive(s_send_mutex);
}

