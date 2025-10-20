#include "net/ws_server.h"

#include <stdlib.h>
#include <string.h>

#include "data_model.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "tasks/t_io.h"
#include "time_sync.h"

static const char *TAG = "sensor_ws";
static SemaphoreHandle_t s_send_mutex;

static void handle_rx(const uint8_t *data, size_t len, bool is_text) {
    (void)is_text;
    io_command_t cmd;
    if (data_model_parse_command(data, len, &cmd)) {
        if (!t_io_post_command(&cmd)) {
            ESP_LOGW(TAG, "IO command queue full");
            (void)sensor_ws_server_send_ack(cmd.seq_id, false, "queue_full");
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
    proto_buffer_t buf = {0};
    if (data_model_prepare_sensor_update(&buf)) {
        ws_server_send(buf.data, buf.len, buf.is_text);
        proto_buffer_free(&buf);
    }
    xSemaphoreGive(s_send_mutex);
}

void sensor_ws_server_send_heartbeat(const proto_buffer_t *packet) {
    if (packet == NULL || packet->data == NULL || packet->len == 0) {
        return;
    }
    if (s_send_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(s_send_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    ws_server_send(packet->data, packet->len, packet->is_text);
    xSemaphoreGive(s_send_mutex);
}

esp_err_t sensor_ws_server_send_ack(uint32_t ref_seq_id, bool success, const char *reason) {
    proto_buffer_t buf = {0};
    uint32_t crc = 0;
    uint32_t seq = data_model_next_sequence();
    bool ok = proto_encode_command_ack(ref_seq_id, success, reason, time_sync_get_monotonic_ms(), seq, &buf, &crc);
    if (!ok) {
        return ESP_FAIL;
    }
    esp_err_t err = ws_server_send(buf.data, buf.len, buf.is_text);
    proto_buffer_free(&buf);
    return err;
}

