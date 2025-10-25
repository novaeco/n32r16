#include "net/ws_client.h"

#include "common/net/mdns_helper.h"
#include "common/net/wifi_manager.h"
#include "common/net/ws_client.h"
#include "common/proto/messages.h"
#include "common/util/base64_utils.h"
#include "common/util/monotonic.h"
#include "prefs_store.h"
#include "cert_store.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/ip6_addr.h"
#include "mdns.h"
#include "nvs.h"
#include "sdkconfig.h"
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

static const char *TAG = "hmi_ws";

static hmi_data_model_t *s_model;
static bool s_use_cbor;
static uint32_t s_next_command_seq;
static char s_discovered_server_name[64];
static uint8_t s_sec2_salt[32];
static uint8_t s_sec2_verifier[384];
static wifi_manager_sec2_params_t s_sec2_params = {
    .salt = s_sec2_salt,
    .verifier = s_sec2_verifier,
};
static bool s_sec2_loaded;
static bool s_wifi_ready;

#define DISCOVERY_CACHE_NAMESPACE "hmi_net"
#define DISCOVERY_CACHE_URI_KEY "last_uri"
#define DISCOVERY_CACHE_SNI_KEY "last_sni"
#define DISCOVERY_CACHE_TS_KEY "last_ts"

typedef struct {
    char proto[8];
    char path[64];
    char host[64];
} service_metadata_t;

static esp_err_t ensure_sec2_params(void);
static esp_err_t ensure_wifi_ready(void);
static void invalidate_cached_service(void);
static void ws_error_cb(const esp_websocket_event_data_t *event, void *ctx);

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

static void set_discovered_server_name(const char *name)
{
    if (!name || !name[0]) {
        s_discovered_server_name[0] = '\0';
        return;
    }
    strlcpy(s_discovered_server_name, name, sizeof(s_discovered_server_name));
}

static void invalidate_cached_service(void)
{
    esp_err_t err = hmi_prefs_store_init();
    if (err != ESP_OK) {
        return;
    }
    nvs_handle_t handle = 0;
    err = nvs_open_from_partition("nvs", DISCOVERY_CACHE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return;
    }
    nvs_erase_key(handle, DISCOVERY_CACHE_URI_KEY);
    nvs_erase_key(handle, DISCOVERY_CACHE_SNI_KEY);
    nvs_erase_key(handle, DISCOVERY_CACHE_TS_KEY);
    nvs_commit(handle);
    nvs_close(handle);
}

static const char *select_tls_server_name(const char *fallback_host)
{
    if (CONFIG_HMI_WS_TLS_SNI_OVERRIDE[0] != '\0') {
        return CONFIG_HMI_WS_TLS_SNI_OVERRIDE;
    }
    if (s_discovered_server_name[0] != '\0') {
        return s_discovered_server_name;
    }
    if (fallback_host && fallback_host[0] != '\0') {
        return fallback_host;
    }
    return NULL;
}

static esp_err_t ensure_sec2_params(void)
{
    if (s_sec2_loaded) {
        return ESP_OK;
    }
    size_t salt_len = 0;
    size_t verifier_len = 0;
    esp_err_t err = base64_utils_decode(CONFIG_HMI_PROV_SEC2_SALT_BASE64, s_sec2_salt, sizeof(s_sec2_salt), &salt_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to decode SRP salt: %s", esp_err_to_name(err));
        return err;
    }
    err = base64_utils_decode(CONFIG_HMI_PROV_SEC2_VERIFIER_BASE64, s_sec2_verifier, sizeof(s_sec2_verifier), &verifier_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to decode SRP verifier: %s", esp_err_to_name(err));
        return err;
    }
    s_sec2_params.salt_len = salt_len;
    s_sec2_params.verifier_len = verifier_len;
    s_sec2_loaded = true;
    return ESP_OK;
}

static esp_err_t ensure_wifi_ready(void)
{
    if (s_wifi_ready) {
        return ESP_OK;
    }
    esp_err_t err = ensure_sec2_params();
    if (err != ESP_OK) {
        return err;
    }
    wifi_manager_config_t wifi_cfg = {
        .power_save = false,
        .service_name_suffix = CONFIG_HMI_PROV_SERVICE_NAME,
        .pop = CONFIG_HMI_PROV_POP,
        .service_key = NULL,
#if CONFIG_HMI_PROV_PREFER_BLE
        .prefer_ble = true,
#else
        .prefer_ble = false,
#endif
        .force_provisioning = false,
        .provisioning_timeout_ms = CONFIG_HMI_PROV_TIMEOUT_MS,
        .connect_timeout_ms = CONFIG_HMI_PROV_CONNECT_TIMEOUT_MS,
        .max_connect_attempts = CONFIG_HMI_PROV_MAX_ATTEMPTS,
        .sec2_params = &s_sec2_params,
        .sec2_username = CONFIG_HMI_PROV_SEC2_USERNAME,
    };
    err = wifi_manager_start(&wifi_cfg);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_wifi_ready = true;
        return ESP_OK;
    }
    return err;
}

static bool load_cached_service(char *uri, size_t uri_len)
{
    if (!uri || uri_len == 0) {
        return false;
    }
    esp_err_t err = hmi_prefs_store_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "secure NVS unavailable: %s", esp_err_to_name(err));
        return false;
    }

    nvs_handle_t handle = 0;
    err = nvs_open_from_partition("nvs", DISCOVERY_CACHE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t required = uri_len;
    err = nvs_get_str(handle, DISCOVERY_CACHE_URI_KEY, uri, &required);
    if (err == ESP_OK) {
        size_t sni_len = sizeof(s_discovered_server_name);
        err = nvs_get_str(handle, DISCOVERY_CACHE_SNI_KEY, s_discovered_server_name, &sni_len);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            s_discovered_server_name[0] = '\0';
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        uint32_t last_seen = 0;
        err = nvs_get_u32(handle, DISCOVERY_CACHE_TS_KEY, &last_seen);
        if (err == ESP_OK) {
            uint32_t now = monotonic_time_ms();
            uint32_t age = now - last_seen;
            uint64_t ttl_ms = (uint64_t)CONFIG_HMI_DISCOVERY_CACHE_TTL_MINUTES * 60000ULL;
            if (ttl_ms > 0 && (uint64_t)age > ttl_ms) {
                ESP_LOGI(TAG, "Cached sensor endpoint expired (%u ms > %" PRIu64 " ms)", age, ttl_ms);
                err = ESP_ERR_INVALID_STATE;
            }
        }
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to load discovery cache: %s", esp_err_to_name(err));
        if (err == ESP_ERR_INVALID_STATE) {
            invalidate_cached_service();
        }
        return false;
    }
    ESP_LOGI(TAG, "Using cached sensor endpoint %s", uri);
    return true;
}

static void store_cached_service(const char *uri)
{
    if (!uri || !uri[0]) {
        return;
    }
    esp_err_t err = hmi_prefs_store_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "secure NVS unavailable: %s", esp_err_to_name(err));
        return;
    }

    nvs_handle_t handle = 0;
    err = nvs_open_from_partition("nvs", DISCOVERY_CACHE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open discovery cache namespace: %s", esp_err_to_name(err));
        return;
    }

    esp_err_t set_err = nvs_set_str(handle, DISCOVERY_CACHE_URI_KEY, uri);
    if (set_err == ESP_OK) {
        const char *sni = s_discovered_server_name[0] ? s_discovered_server_name : "";
        set_err = nvs_set_str(handle, DISCOVERY_CACHE_SNI_KEY, sni);
    }
    if (set_err == ESP_OK) {
        uint32_t now = monotonic_time_ms();
        set_err = nvs_set_u32(handle, DISCOVERY_CACHE_TS_KEY, now);
    }
    if (set_err == ESP_OK) {
        set_err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (set_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist discovery cache: %s", esp_err_to_name(set_err));
    }
}

static void service_metadata_init(service_metadata_t *meta)
{
    if (!meta) {
        return;
    }
    strlcpy(meta->proto, "wss", sizeof(meta->proto));
    strlcpy(meta->path, "/ws", sizeof(meta->path));
    meta->host[0] = '\0';
}

static bool is_token_valid(const char *value)
{
    if (!value) {
        return false;
    }
    size_t len = strlen(value);
    if (len == 0 || len >= 32) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        char c = value[i];
        if (!(c == '-' || c == '+' || c == '.' || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            return false;
        }
    }
    return true;
}

static void sanitize_path(char *dest, size_t dest_len, const char *value)
{
    if (!dest || dest_len == 0) {
        return;
    }
    if (!value || !value[0]) {
        strlcpy(dest, "/ws", dest_len);
        return;
    }
    if (value[0] != '/') {
        char tmp[64];
        int written = snprintf(tmp, sizeof(tmp), "/%s", value);
        if (written > 0) {
            strlcpy(dest, tmp, dest_len);
        } else {
            strlcpy(dest, "/ws", dest_len);
        }
        return;
    }
    strlcpy(dest, value, dest_len);
}

static void apply_txt_metadata(service_metadata_t *meta, const mdns_result_t *result)
{
    if (!meta || !result || !result->txt || result->txt_count == 0) {
        return;
    }
    for (size_t i = 0; i < result->txt_count; ++i) {
        const mdns_txt_item_t *item = &result->txt[i];
        if (!item->key || !item->value) {
            continue;
        }
        if (strcmp(item->key, "proto") == 0) {
            if (is_token_valid(item->value)) {
                strlcpy(meta->proto, item->value, sizeof(meta->proto));
            }
        } else if (strcmp(item->key, "path") == 0) {
            sanitize_path(meta->path, sizeof(meta->path), item->value);
        } else if (strcmp(item->key, "uri") == 0) {
            const char *value = item->value;
            const char *scheme_sep = strstr(value, "://");
            if (scheme_sep && (size_t)(scheme_sep - value) < sizeof(meta->proto)) {
                char proto[sizeof(meta->proto)] = {0};
                size_t proto_len = (size_t)(scheme_sep - value);
                memcpy(proto, value, proto_len);
                proto[proto_len] = '\0';
                if (is_token_valid(proto)) {
                    strlcpy(meta->proto, proto, sizeof(meta->proto));
                }
                const char *path = strchr(scheme_sep + 3, '/');
                if (path) {
                    sanitize_path(meta->path, sizeof(meta->path), path);
                }
            }
        } else if (strcmp(item->key, "host") == 0 || strcmp(item->key, "sni") == 0) {
            if (strlen(item->value) < sizeof(meta->host)) {
                strlcpy(meta->host, item->value, sizeof(meta->host));
            }
        }
    }
}

static bool build_uri_from_result(const mdns_result_t *result, char *uri, size_t uri_len)
{
    if (!result || !uri || uri_len == 0) {
        return false;
    }
    service_metadata_t meta;
    service_metadata_init(&meta);
    apply_txt_metadata(&meta, result);

    if (strcmp(meta.proto, "wss") != 0 && strcmp(meta.proto, "ws") != 0) {
        ESP_LOGW(TAG, "Skipping service with unsupported proto=%s", meta.proto);
        return false;
    }

    const mdns_ip_addr_t *addr = result->addr;
    while (addr) {
        char ip[INET6_ADDRSTRLEN + 1] = {0};
        char host[INET6_ADDRSTRLEN + 3] = {0};
        if (addr->addr.type == IPADDR_TYPE_V6) {
            ip6addr_ntoa_r(&addr->addr.u_addr.ip6, ip, sizeof(ip));
            int written = snprintf(host, sizeof(host), "[%s]", ip);
            if (written < 0 || (size_t)written >= sizeof(host)) {
                addr = addr->next;
                continue;
            }
        } else if (addr->addr.type == IPADDR_TYPE_V4) {
            ip4addr_ntoa_r(&addr->addr.u_addr.ip4, ip, sizeof(ip));
            strlcpy(host, ip, sizeof(host));
        } else {
            addr = addr->next;
            continue;
        }

        int written = snprintf(uri, uri_len, "%s://%s:%u%s", meta.proto, host, result->port, meta.path);
        if (written > 0 && (size_t)written < uri_len) {
            if (meta.host[0]) {
                set_discovered_server_name(meta.host);
            } else if (result->hostname && result->hostname[0]) {
                set_discovered_server_name(result->hostname);
            } else {
                set_discovered_server_name(NULL);
            }
            ESP_LOGI(TAG, "Discovered sensor endpoint %s", uri);
            return true;
        }
        addr = addr->next;
    }
    return false;
}

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

static void ws_error_cb(const esp_websocket_event_data_t *event, void *ctx)
{
    (void)ctx;
    if (!event) {
        return;
    }
    ESP_LOGW(TAG, "WebSocket transport error, purging cached discovery metadata");
    invalidate_cached_service();
}

static bool discover_service(char *uri, size_t uri_len)
{
    set_discovered_server_name(NULL);
    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "mDNS init failed: %s", esp_err_to_name(err));
    }
    const uint32_t timeout_ms = CONFIG_HMI_DISCOVERY_TIMEOUT_MS;
    const int64_t start = esp_timer_get_time() / 1000;
    while (timeout_ms == 0 || (uint32_t)((esp_timer_get_time() / 1000) - start) < timeout_ms) {
        mdns_result_t *results = NULL;
        err = mdns_query_ptr("_hmi-sensor", "_tcp", 3000, 20, &results);
        if (err == ESP_OK && results) {
            mdns_result_t *r = results;
            while (r) {
                if (build_uri_from_result(r, uri, uri_len)) {
                    store_cached_service(uri);
                    mdns_query_results_free(results);
                    return true;
                }
                r = r->next;
            }
            mdns_query_results_free(results);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (timeout_ms != 0) {
            int64_t elapsed = (esp_timer_get_time() / 1000) - start;
            if (elapsed >= timeout_ms) {
                break;
            }
        }
    }
    if (load_cached_service(uri, uri_len)) {
        return true;
    }
    return false;
}

esp_err_t hmi_ws_client_start(hmi_data_model_t *model)
{
    if (!model) {
        return ESP_ERR_INVALID_ARG;
    }
    s_model = model;
#if CONFIG_USE_CBOR
    s_use_cbor = true;
#else
    s_use_cbor = false;
#endif
    s_next_command_seq = 0;

    esp_err_t err = ensure_wifi_ready();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi provisioning failed: %s", esp_err_to_name(err));
        return err;
    }

    char uri[128];
    const char *fallback_host = NULL;
    if (!discover_service(uri, sizeof(uri))) {
        snprintf(uri, sizeof(uri), "wss://%s:%u/ws", CONFIG_HMI_SENSOR_HOSTNAME, CONFIG_HMI_SENSOR_PORT);
        set_discovered_server_name(CONFIG_HMI_SENSOR_HOSTNAME);
        fallback_host = CONFIG_HMI_SENSOR_HOSTNAME;
    }
    const char *tls_server_name = select_tls_server_name(fallback_host);
    ESP_LOGI(TAG, "Connecting to %s", uri);
    if (tls_server_name) {
        ESP_LOGI(TAG, "Using TLS server name %s", tls_server_name);
    }
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
        .tls_server_name = tls_server_name,
        .error_cb = ws_error_cb,
        .error_ctx = NULL,
    };
    esp_err_t start_err = ws_client_start(&cfg, ws_rx, NULL);
    if (start_err == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket start failed: %s", esp_err_to_name(start_err));
        invalidate_cached_service();
    }
    return start_err;
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
