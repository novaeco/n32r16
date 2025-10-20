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

static void wifi_event_forwarder(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            data_model_set_wifi_state(NETWORK_STATE_CONNECTING);
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            data_model_set_wifi_state(NETWORK_STATE_CONNECTING);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        data_model_set_wifi_state(NETWORK_STATE_READY);
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    data_model_init();

    ESP_ERROR_CHECK(i2c_bus_init(I2C_NUM_0, I2C_SDA_GPIO, I2C_SCL_GPIO, 400000));

    wifi_sta_config_t wifi_cfg = {
        .ssid = CONFIG_SENSOR_NODE_WIFI_SSID,
        .password = CONFIG_SENSOR_NODE_WIFI_PASS,
        .auto_reconnect = true,
    };
    data_model_set_wifi_state(NETWORK_STATE_CONNECTING);
    ESP_ERROR_CHECK(wifi_sta_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_forwarder, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_forwarder, NULL));
    wifi_sta_wait_connected();
    time_sync_init();

    ESP_ERROR_CHECK(mdns_start_service("sensor-node", "Sensor Node", 8443, "_hmi-sensor"));
    ESP_ERROR_CHECK(sensor_ws_server_start());

    t_io_start();
    t_sensors_start();
    t_heartbeat_start();

    ESP_LOGI(TAG, "Sensor node initialized");
}

