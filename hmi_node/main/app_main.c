#include "app_main.h"

#include "cert_store.h"
#include "common/net/wifi_manager.h"
#include "common/ota/ota_update.h"
#include "data_model.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tasks/t_heartbeat.h"
#include "tasks/t_net_rx.h"
#include "tasks/t_ui.h"

static const char *TAG = "hmi_main";

static bool wait_for_wifi(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    const uint32_t step = 200;
    while (elapsed < timeout_ms) {
        if (wifi_manager_is_connected()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(step));
        elapsed += step;
    }
    return wifi_manager_is_connected();
}

void app_main(void)
{
    ESP_LOGI(TAG, "Booting HMI node");
    hmi_data_model_t model;
    hmi_data_model_init(&model);

    heartbeat_task_start();
    net_task_start(&model);
    ui_task_start(&model);

    size_t ca_len = 0;
    const uint8_t *ca = cert_store_ca_cert(&ca_len);
    ota_update_config_t ota_cfg = {
        .url = CONFIG_HMI_OTA_URL,
        .cert_pem = ca,
        .cert_len = ca_len,
        .auto_reboot = true,
        .check_delay_ms = 5000,
        .wait_for_connectivity = wait_for_wifi,
    };
    ESP_ERROR_CHECK(ota_update_schedule(&ota_cfg));
}
