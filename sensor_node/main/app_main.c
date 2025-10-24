#include "app_main.h"

#include "board_pins.h"
#include "cert_store.h"
#include "common/net/wifi_manager.h"
#include "common/ota/ota_update.h"
#include "common/util/monotonic.h"
#include "data_model.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "net/ws_server.h"
#include "onewire_bus_manager.h"
#include "tasks/t_heartbeat.h"
#include "tasks/t_io.h"
#include "tasks/t_sensors.h"
#include "sdkconfig.h"

static const char *TAG = "sensor_main";

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
    ESP_LOGI(TAG, "Booting sensor node firmware");
    ESP_ERROR_CHECK(i2c_bus_init());

    onewire_bus_handle_t ow_bus;
    ESP_ERROR_CHECK(onewire_bus_manager_init(SENSOR_NODE_ONEWIRE_PIN, &ow_bus));

    sensor_data_model_t model;
    data_model_init(&model);

    sensors_task_start(&model, ow_bus);
    io_task_start(&model);
    heartbeat_task_start();
    sensor_ws_server_start(&model);

    size_t ca_len = 0;
    const uint8_t *ca = cert_store_ca_cert(&ca_len);
    ota_update_config_t ota_cfg = {
        .url = CONFIG_SENSOR_OTA_URL,
        .cert_pem = ca,
        .cert_len = ca_len,
        .auto_reboot = true,
        .check_delay_ms = 5000,
        .wait_for_connectivity = wait_for_wifi,
    };
    ESP_ERROR_CHECK(ota_update_schedule(&ota_cfg));

    const bool use_cbor =
#if CONFIG_USE_CBOR
        true;
#else
        false;
#endif

    while (true) {
        data_model_set_timestamp(&model, monotonic_time_ms());
        if (data_model_should_publish(&model, 0.3f, 1.0f)) {
            data_model_increment_seq(&model);
            sensor_ws_server_send_update(&model, use_cbor);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
