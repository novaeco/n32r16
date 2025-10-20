#include "tasks/t_heartbeat.h"

#include <stdlib.h>
#include <string.h>

#include "board_pins.h"
#include "data_model.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "net/ws_server.h"
#include "onewire_bus.h"
#include "time_sync.h"

#define HEARTBEAT_TASK_STACK 4096
#define HEARTBEAT_TASK_PRIO 4

static const char *TAG = "t_heartbeat";

static float compute_cpu_percent(void) {
    static uint32_t last_total = 0;
    static uint32_t last_idle = 0;
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *status = (TaskStatus_t *)malloc(task_count * sizeof(TaskStatus_t));
    if (status == NULL) {
        ESP_LOGW(TAG, "runtime alloc failed");
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

static void heartbeat_task(void *arg) {
    (void)arg;
    gpio_set_direction(HEARTBEAT_LED_GPIO, GPIO_MODE_OUTPUT);
    bool level = false;

    while (true) {
        float cpu_percent = compute_cpu_percent();
        wifi_ap_record_t ap = {0};
        int8_t rssi = -127;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            rssi = ap.rssi;
        }

        system_metrics_t metrics = {
            .cpu_load = cpu_percent,
            .wifi_rssi = rssi,
            .uptime_ms = (uint32_t)time_sync_get_monotonic_ms(),
            .i2c_errors = i2c_bus_get_error_count(),
            .onewire_errors = onewire_bus_get_error_count(),
        };
        data_model_set_metrics(&metrics);

        proto_buffer_t hb = {0};
        if (data_model_build_heartbeat(&hb)) {
            sensor_ws_server_send_heartbeat(&hb);
            proto_buffer_free(&hb);
        }

        level = !level;
        gpio_set_level(HEARTBEAT_LED_GPIO, level);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void t_heartbeat_start(void) {
    xTaskCreate(heartbeat_task, "t_heartbeat", HEARTBEAT_TASK_STACK, NULL, HEARTBEAT_TASK_PRIO, NULL);
}

