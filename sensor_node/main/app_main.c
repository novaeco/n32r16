#include "app_main.h"

#include "board_pins.h"
#include "data_model.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "mdns_support.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "tasks/t_heartbeat.h"
#include "tasks/t_io.h"
#include "tasks/t_sensors.h"
#include "time_sync.h"
#include "wifi_sta.h"
#include "net/ws_server.h"

static const char *TAG = "sensor_main";

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    time_sync_init();
    data_model_init();

    ESP_ERROR_CHECK(i2c_bus_init(I2C_NUM_0, I2C_SDA_GPIO, I2C_SCL_GPIO, 400000));

    wifi_sta_config_t wifi_cfg = {
        .ssid = CONFIG_SENSOR_NODE_WIFI_SSID,
        .password = CONFIG_SENSOR_NODE_WIFI_PASS,
        .auto_reconnect = true,
    };
    ESP_ERROR_CHECK(wifi_sta_init(&wifi_cfg));
    wifi_sta_wait_connected();

    ESP_ERROR_CHECK(mdns_start_service("sensor-node", "Sensor Node", 8080, "_hmi-sensor"));
    ESP_ERROR_CHECK(sensor_ws_server_start());

    t_io_start();
    t_sensors_start();
    t_heartbeat_start();

    ESP_LOGI(TAG, "Sensor node initialized");
}

