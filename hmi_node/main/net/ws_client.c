#include "net/ws_client.h"

#include "common/net/mdns_helper.h"
#include "common/net/wifi_manager.h"
#include "common/net/ws_client.h"
#include "common/proto/messages.h"
#include "common/util/monotonic.h"
#include "cert_store.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "mdns.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "hmi_ws";

static hmi_data_model_t *s_model;
static bool s_use_cbor;
static uint32_t s_next_command_seq;

static void handle_sensor_update(const uint8_t *data, size_t len, uint32_t crc)
{
    proto_sensor_update_t update;
    if (!proto_decode_sensor_update(data, len, s_use_cbor, &update, crc)) {
        ESP_LOGW(TAG, "Failed to decode sensor update");
        hmi_data_model_set_crc_status(s_model, false);
        return;
    }
    hmi_data_model_set_update(s_model, &update);
    hmi_data_model_set_crc_status(s_model, true);
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
                        snprintf(uri, uri_len, "wss://%s:%u/ws", ip, r->port);
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
    s_next_command_seq = 0;

    wifi_manager_config_t wifi_cfg = {
        .power_save = false,
        .service_name_suffix = CONFIG_HMI_PROV_SERVICE_NAME,
        .pop = CONFIG_HMI_PROV_POP,
    };
    ESP_ERROR_CHECK(wifi_manager_start(&wifi_cfg));

    char uri[128];
    if (!discover_service(uri, sizeof(uri))) {
        snprintf(uri, sizeof(uri), "wss://%s:%u/ws", CONFIG_HMI_SENSOR_HOSTNAME, CONFIG_HMI_SENSOR_PORT);
    }
    ESP_LOGI(TAG, "Connecting to %s", uri);
    size_t ca_len = 0;
    const uint8_t *ca = cert_store_ca_cert(&ca_len);
    ws_client_config_t cfg = {
        .uri = uri,
        .auth_token = CONFIG_HMI_WS_AUTH_TOKEN,
        .ca_cert = ca,
        .ca_cert_len = ca_len,
        .skip_common_name_check = false,
        .reconnect_min_delay_ms = 2000,
        .reconnect_max_delay_ms = 60000,
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

esp_err_t hmi_ws_client_send_command(const proto_command_t *cmd)
{
    if (!cmd) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!hmi_ws_client_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    proto_command_t local = *cmd;
    local.timestamp_ms = monotonic_time_ms();
    local.sequence_id = ++s_next_command_seq;

    uint8_t payload[PROTO_MAX_COMMAND_SIZE];
    size_t payload_len = sizeof(payload);
    uint32_t crc = 0;
    if (!proto_encode_command_into(&local, s_use_cbor, payload, &payload_len, &crc)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (payload_len + sizeof(uint32_t) > sizeof(payload) + sizeof(uint32_t)) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t frame[PROTO_MAX_COMMAND_SIZE + sizeof(uint32_t)];
    memcpy(frame, &crc, sizeof(uint32_t));
    memcpy(frame + sizeof(uint32_t), payload, payload_len);
    return ws_client_send(frame, payload_len + sizeof(uint32_t));
}
