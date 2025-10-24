#include "tasks/t_ui.h"

#include "display/lvgl_port.h"
#include "display/ui_screens.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static hmi_data_model_t *s_model;

static void ui_task(void *arg)
{
    (void)arg;
    lvgl_port_init();
    ui_init();

    proto_sensor_update_t update = {0};
    while (true) {
        if (hmi_data_model_get_update(s_model, &update)) {
            ui_update_sensor_data(&update);
        }
        ui_update_connection_status(hmi_data_model_is_connected(s_model));
        ui_process();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void ui_task_start(hmi_data_model_t *model)
{
    s_model = model;
    xTaskCreatePinnedToCore(ui_task, "t_ui", 6144, NULL, 4, NULL, 1);
}
