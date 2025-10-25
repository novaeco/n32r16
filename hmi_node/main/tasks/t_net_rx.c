#include "tasks/t_net_rx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/ws_client.h"
#include "esp_log.h"

static hmi_data_model_t *s_model;
static const char *TAG = "t_net";

static void net_task(void *arg)
{
    (void)arg;
    bool ws_started = false;
    while (true) {
        if (!ws_started) {
            esp_err_t err = hmi_ws_client_start(s_model);
            if (err == ESP_OK) {
                ws_started = true;
            } else {
                ESP_LOGE(TAG, "Failed to start WS client: %s", esp_err_to_name(err));
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }
        bool connected = hmi_ws_client_is_connected();
        hmi_data_model_set_connected(s_model, connected);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void net_task_start(hmi_data_model_t *model)
{
    s_model = model;
    xTaskCreatePinnedToCore(net_task, "t_net", 4096, NULL, 5, NULL, 0);
}
