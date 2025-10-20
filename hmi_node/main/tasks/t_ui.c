#include "tasks/t_ui.h"

#include "data_model.h"
#include "display/lvgl_port.h"
#include "display/ui_screens.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net/ws_client.h"
#include "touch/gt911.h"

#define UI_TASK_STACK 8192
#define UI_TASK_PRIO 6

static const char *TAG = "t_ui";
static SemaphoreHandle_t s_update_sem;

static void ws_update_callback(void) {
    if (s_update_sem != NULL) {
        xSemaphoreGive(s_update_sem);
    }
}

static void ui_task(void *arg) {
    (void)arg;
    lvgl_port_init();
    if (gt911_init() != ESP_OK) {
        ESP_LOGW(TAG, "GT911 initialization failed, running without touch");
    } else {
        gt911_register_input_device();
    }
    ui_screens_init();

    while (true) {
        if (s_update_sem != NULL && xSemaphoreTake(s_update_sem, 0) == pdTRUE) {
            hmi_data_snapshot_t snapshot;
            if (hmi_data_model_get_snapshot(&snapshot)) {
            lvgl_port_lock();
                ui_screens_update(&snapshot);
            lvgl_port_unlock();
            }
        }
        lvgl_port_lock();
        lv_timer_handler();
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void t_ui_start(void) {
    s_update_sem = xSemaphoreCreateBinary();
    hmi_ws_client_set_update_callback(ws_update_callback);
    xTaskCreate(ui_task, "t_ui", UI_TASK_STACK, NULL, UI_TASK_PRIO, NULL);
}

