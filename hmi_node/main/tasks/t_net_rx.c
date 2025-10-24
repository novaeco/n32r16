#include "tasks/t_net_rx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net/ws_client.h"

static hmi_data_model_t *s_model;

static void net_task(void *arg)
{
    (void)arg;
    hmi_ws_client_start(s_model);
    while (true) {
        bool connected = hmi_ws_client_is_connected();
        hmi_data_model_set_connected(s_model, connected);
        if (!connected) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            hmi_ws_client_stop();
            hmi_ws_client_start(s_model);
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void net_task_start(hmi_data_model_t *model)
{
    s_model = model;
    xTaskCreatePinnedToCore(net_task, "t_net", 4096, NULL, 5, NULL, 0);
}
