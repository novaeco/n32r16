#include "net/ws_client.h"

#include "common/net/mdns_helper.h"
#include "common/net/wifi_manager.h"
#include "common/net/ws_client.h"
#include "common/proto/messages.h"
#include "esp_log.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "sdkconfig.h"

static const char *TAG = "hmi_ws";

static hmi_data_model_t *s_model;
static bool s_use_cbor;

static void handle_sensor_update(const uint8_t *data, size_t len, uint32_t crc)
{
    proto_sensor_update_t update;
    if (!proto_decode_sensor_update(data, len, s_use_cbor, &update, crc)) {
        ESP_LOGW(TAG, "Failed to decode sensor update");
        return;
    }
    hmi_data_model_set_update(s_model, &update);
}

static void ws_rx(const uint8_t *data, size_t len, uint32_t crc, void *ctx)
{
    (void)ctx;
    handle_sensor_update(data, len, crc);
}

static bool discover_service(char *uri, size_t uri_len)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(err));
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        mdns_result_t *results = NULL;
        err = mdns_query_ptr("_hmi-sensor", "_tcp", 3000, 20, &results);
        if (err == ESP_OK && results) {
            mdns_result_t *r = results;
            while (r) {
                if (r->addr) {
                    char ip[64];
                    if (r->addr->addr.type == IPADDR_TYPE_V4) {
                        ip4addr_ntoa_r(&r->addr->addr.u_addr.ip4, ip, sizeof(ip));
                        snprintf(uri, uri_len, "ws://%s:%u/ws", ip, r->port);
                        mdns_query_results_free(results);
                        return true;
                    }
                }
                r = r->next;
            }
            mdns_query_results_free(results);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    return false;
}

void hmi_ws_client_start(hmi_data_model_t *model)
{
    s_model = model;
#if CONFIG_USE_CBOR
    s_use_cbor = true;
#else
    s_use_cbor = false;
#endif

    wifi_manager_config_t wifi_cfg = {
        .ssid = CONFIG_HMI_WIFI_SSID,
        .password = CONFIG_HMI_WIFI_PASSWORD,
        .power_save = false,
    };
    ESP_ERROR_CHECK(wifi_manager_start_sta(&wifi_cfg));

    char uri[128];
    if (!discover_service(uri, sizeof(uri))) {
        snprintf(uri, sizeof(uri), "ws://%s:%u/ws", CONFIG_HMI_SENSOR_HOSTNAME, CONFIG_HMI_SENSOR_PORT);
    }
    ESP_LOGI(TAG, "Connecting to %s", uri);
    ws_client_config_t cfg = {
        .uri = uri,
        .use_ssl = false,
    };
    esp_err_t start_err = ws_client_start(&cfg, ws_rx, NULL);
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket start failed: %s", esp_err_to_name(start_err));
    }
}

void hmi_ws_client_stop(void)
{
    ws_client_stop();
}

bool hmi_ws_client_is_connected(void)
{
    return ws_client_is_connected();
}
