#include "tasks/t_heartbeat.h"

#include <stdlib.h>
#include <string.h>

#include "data_model.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/ws_client.h"
#include "proto_codec.h"
#include "time_sync.h"

#define HMI_HEARTBEAT_GPIO GPIO_NUM_1
#define HEARTBEAT_STACK 4096
#define HEARTBEAT_PRIO 3

static const char *TAG = "t_hb";

static float calculate_cpu_percent(void) {
    static uint32_t last_total = 0;
    static uint32_t last_idle = 0;
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *status = (TaskStatus_t *)malloc(task_count * sizeof(TaskStatus_t));
    if (status == NULL) {
        return 0.0f;
    }
    uint32_t total_runtime = 0;
    UBaseType_t fetched = uxTaskGetSystemState(status, task_count, &total_runtime);
    uint32_t idle_runtime = 0;
    for (UBaseType_t i = 0; i < fetched; ++i) {
        if (strncmp(status[i].pcTaskName, "IDLE", 4) == 0) {
            idle_runtime += status[i].ulRunTimeCounter;
        }
    }
    free(status);
    if (last_total == 0 || total_runtime <= last_total) {
        last_total = total_runtime;
        last_idle = idle_runtime;
        return 0.0f;
    }
    float delta_total = (float)(total_runtime - last_total);
    float delta_idle = (float)(idle_runtime - last_idle);
    last_total = total_runtime;
    last_idle = idle_runtime;
    if (delta_total <= 0.0f) {
        return 0.0f;
    }
    float load = 1.0f - (delta_idle / delta_total);
    if (load < 0.0f) {
        load = 0.0f;
    }
    if (load > 1.0f) {
        load = 1.0f;
    }
    return load * 100.0f;
}

static void send_heartbeat(void) {
    cJSON *payload = cJSON_CreateObject();
    if (payload == NULL) {
        return;
    }
    cJSON_AddNumberToObject(payload, "cpu", calculate_cpu_percent());
    cJSON_AddNumberToObject(payload, "uptime", time_sync_get_monotonic_ms());

    proto_buffer_t buf = {0};
    uint32_t seq = hmi_data_model_next_command_seq();
    uint32_t crc = 0;
    bool ok = proto_encode_heartbeat(payload, time_sync_get_monotonic_ms(), seq, &buf, &crc);
    cJSON_Delete(payload);
    if (!ok) {
        return;
    }
    esp_err_t err = hmi_ws_client_send(&buf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Heartbeat send failed: %s", esp_err_to_name(err));
    }
    proto_buffer_free(&buf);
}

static void heartbeat_task(void *arg) {
    (void)arg;
    gpio_set_direction(HMI_HEARTBEAT_GPIO, GPIO_MODE_OUTPUT);
    bool level = false;
    while (true) {
        level = !level;
        gpio_set_level(HMI_HEARTBEAT_GPIO, level);
        send_heartbeat();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void t_hmi_heartbeat_start(void) {
    xTaskCreate(heartbeat_task, "t_hb", HEARTBEAT_STACK, NULL, HEARTBEAT_PRIO, NULL);
}

