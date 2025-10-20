#include "app_main.h"

#include "data_model.h"
#include "esp_event.h"
#include "esp_log.h"
#include "mdns.h"
#include "net/ws_client.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "tasks/t_hmi_heartbeat.h"
#include "tasks/t_net_rx.h"
#include "tasks/t_ui.h"
#include "time_sync.h"
#include "wifi_sta.h"

static const char *TAG = "hmi_main";

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    time_sync_init();
    hmi_data_model_init();

    wifi_sta_config_t wifi_cfg = {
        .ssid = CONFIG_HMI_NODE_WIFI_SSID,
        .password = CONFIG_HMI_NODE_WIFI_PASS,
        .auto_reconnect = true,
    };
    ESP_ERROR_CHECK(wifi_sta_init(&wifi_cfg));
    wifi_sta_wait_connected();
    ESP_ERROR_CHECK(mdns_init());

    t_ui_start();
    t_net_rx_start();
    t_hmi_heartbeat_start();

    ESP_LOGI(TAG, "HMI node initialized");
}

