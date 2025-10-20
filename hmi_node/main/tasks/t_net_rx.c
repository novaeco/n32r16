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
    if (hmi_ws_client_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client");
    }
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void t_net_rx_start(void) {
    xTaskCreate(net_task, "t_net", NET_TASK_STACK, NULL, NET_TASK_PRIO, NULL);
}

