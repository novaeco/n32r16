#include "tasks/t_net_rx.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/ws_client.h"

#define NET_TASK_STACK 4096
#define NET_TASK_PRIO 5

static const char *TAG = "t_net_rx";

static void net_task(void *arg) {
    (void)arg;
    while (true) {
        esp_err_t err = hmi_ws_client_start();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "WebSocket client connected");
            break;
        }
        ESP_LOGW(TAG, "WebSocket start failed: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void t_net_rx_start(void) {
    xTaskCreate(net_task, "t_net", NET_TASK_STACK, NULL, NET_TASK_PRIO, NULL);
}

