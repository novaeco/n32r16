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

static void hmi_wifi_event_forwarder(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            hmi_data_model_set_wifi_connected(false);
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            hmi_data_model_set_wifi_connected(false);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        hmi_data_model_set_wifi_connected(true);
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    hmi_data_model_init();

    wifi_sta_config_t wifi_cfg = {
        .ssid = CONFIG_HMI_NODE_WIFI_SSID,
        .password = CONFIG_HMI_NODE_WIFI_PASS,
        .auto_reconnect = true,
    };
    hmi_data_model_set_wifi_connected(false);
    ESP_ERROR_CHECK(wifi_sta_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &hmi_wifi_event_forwarder, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &hmi_wifi_event_forwarder, NULL));
    wifi_sta_wait_connected();
    time_sync_init();
    ESP_ERROR_CHECK(mdns_init());

    t_ui_start();
    t_net_rx_start();
    t_hmi_heartbeat_start();

    ESP_LOGI(TAG, "HMI node initialized");
}

