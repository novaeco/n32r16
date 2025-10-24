#include "app_main.h"

#include "data_model.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tasks/t_heartbeat.h"
#include "tasks/t_net_rx.h"
#include "tasks/t_ui.h"

static const char *TAG = "hmi_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Booting HMI node");
    hmi_data_model_t model;
    hmi_data_model_init(&model);

    heartbeat_task_start();
    net_task_start(&model);
    ui_task_start(&model);
}
