#include "wifi_manager.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "nvs_flash_secure.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"
#include "wifi_provisioning/security1.h"
#include <string.h>

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_PROV_DONE_BIT BIT2

static const char *TAG = "wifi";

static EventGroupHandle_t s_wifi_event_group;
static bool s_connected = false;
static TimerHandle_t s_reconnect_timer;
static uint32_t s_backoff_ms = 1000;
static bool s_force_reprovision;

/**
 * @brief Timer callback that retries Wi-Fi connection after a backoff delay.
 *
 * @param timer Handle of the FreeRTOS timer invoking the callback.
 * @return void
 */
static void reconnect_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    ESP_LOGI(TAG, "Reconnecting to AP");
    esp_wifi_connect();
}

/**
 * @brief Handle Wi-Fi and IP events to maintain STA connectivity state.
 *
 * @param arg User context (unused).
 * @param event_base Event base identifier (WIFI_EVENT or IP_EVENT).
 * @param event_id Event identifier within the base.
 * @param event_data Pointer to event-specific data.
 * @return void
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        if (s_reconnect_timer) {
            if (xTimerIsTimerActive(s_reconnect_timer) == pdPASS) {
                xTimerStop(s_reconnect_timer, 0);
            }
            xTimerChangePeriod(s_reconnect_timer, pdMS_TO_TICKS(s_backoff_ms), 0);
            xTimerStart(s_reconnect_timer, 0);
            if (s_backoff_ms < 32000) {
                s_backoff_ms *= 2;
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        s_backoff_ms = 1000;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief Process provisioning manager events for logging and synchronization.
 *
 * @param user_data Optional user context (unused).
 * @param event Provisioning event identifier.
 * @param event_data Event-specific payload.
 * @return void
 */
static void provisioning_event_handler(void *user_data, wifi_prov_cb_event_t event, void *event_data)
{
    (void)user_data;
    switch (event) {
    case WIFI_PROV_CRED_RECV: {
        wifi_sta_config_t *sta = (wifi_sta_config_t *)event_data;
        ESP_LOGI(TAG, "Received provisioning credentials for SSID %s", (const char *)sta->ssid);
        break;
    }
    case WIFI_PROV_CRED_FAIL: {
        wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
        ESP_LOGE(TAG, "Provisioning failed: reason=%d", (int)*reason);
        break;
    }
    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "Provisioning successful");
        xEventGroupSetBits(s_wifi_event_group, WIFI_PROV_DONE_BIT);
        break;
    case WIFI_PROV_END:
        ESP_LOGI(TAG, "Provisioning terminated");
        wifi_prov_mgr_stop_provisioning();
        break;
    default:
        break;
    }
}

/**
 * @brief Initialize the secure NVS storage required by Wi-Fi provisioning.
 *
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t init_nvs_secure(void)
{
    nvs_sec_cfg_t cfg = {0};
    esp_err_t err = nvs_flash_secure_init(&cfg);
    if (err == ESP_ERR_NVS_SEC_KEY_PART_NOT_FOUND) {
        ESP_LOGI(TAG, "NVS key partition missing, generating new keys");
        ESP_ERROR_CHECK(nvs_flash_generate_keys(&cfg));
        err = nvs_flash_secure_init(&cfg);
    }
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_deinit());
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_secure_init(&cfg);
    }
    ESP_ERROR_CHECK(err);
    return nvs_flash_secure_set_flash(&cfg);
}

/**
 * @brief Lazily initialize the ESP-NETIF and default Wi-Fi STA stack.
 *
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t ensure_netif(void)
{
    static bool initialized = false;
    if (!initialized) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();
        initialized = true;
    }
    return ESP_OK;
}

/**
 * @brief Compose the provisioning service name based on the device MAC address.
 *
 * @param config Wi-Fi manager configuration structure.
 * @param out Output buffer receiving the formatted service name.
 * @param len Length of the output buffer in bytes.
 * @return void
 */
static void format_service_name(const wifi_manager_config_t *config, char *out, size_t len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "%s-%02X%02X%02X", config->service_name_suffix ? config->service_name_suffix : "PROV",
             mac[3], mac[4], mac[5]);
}

/**
 * @brief Start Wi-Fi provisioning if required and connect to the configured network.
 *
 * @param config Pointer to the Wi-Fi manager configuration.
 * @return ESP_OK when the station is connected, otherwise an error code.
 */
esp_err_t wifi_manager_start(const wifi_manager_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(init_nvs_secure());
    ESP_ERROR_CHECK(ensure_netif());

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
    }
    if (!s_reconnect_timer) {
        s_reconnect_timer = xTimerCreate("wifi_reconnect", pdMS_TO_TICKS(1000), pdFALSE, NULL, reconnect_timer_cb);
    }

    wifi_prov_mgr_config_t prov_cfg = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
        .app_event_handler = {
            .event_cb = provisioning_event_handler,
            .user_data = NULL,
        },
    };

    esp_err_t err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    ESP_ERROR_CHECK(wifi_prov_mgr_init(prov_cfg));

    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
    if (s_force_reprovision || config->force_provisioning) {
        provisioned = false;
    }

    if (!provisioned) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_PROV_DONE_BIT | WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
        char service_name[20] = {0};
        format_service_name(config, service_name, sizeof(service_name));
        const char *service_key = config->service_key ? config->service_key : "provision";
        const char *pop = config->pop ? config->pop : "pop";
        wifi_prov_security1_params_t pop_params = {
            .data = (const uint8_t *)pop,
            .len = strlen(pop),
        };
        ESP_LOGI(TAG, "Starting provisioning SSID=%s", service_name);
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, &pop_params, service_name, service_key));

        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_PROV_DONE_BIT,
                                               pdTRUE, pdFALSE, portMAX_DELAY);
        if (!(bits & WIFI_PROV_DONE_BIT)) {
            ESP_LOGE(TAG, "Provisioning aborted");
            wifi_prov_mgr_deinit();
            return ESP_FAIL;
        }
        s_force_reprovision = false;
    } else {
        ESP_LOGI(TAG, "Using stored Wi-Fi credentials");
        xEventGroupSetBits(s_wifi_event_group, WIFI_PROV_DONE_BIT);
    }

    wifi_prov_mgr_deinit();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(config->power_save ? WIFI_PS_MAX_MODEM : WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Failed to connect to provisioned network");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Query the current Wi-Fi station connection status.
 *
 * @return true if connected to an access point, false otherwise.
 */
bool wifi_manager_is_connected(void)
{
    return s_connected;
}

/**
 * @brief Force reprovisioning on the next Wi-Fi manager startup.
 *
 * @return void
 */
void wifi_manager_request_reprovision(void)
{
    s_force_reprovision = true;
    wifi_prov_mgr_reset_provisioning();
}
