#include "wifi_provisioner.h"

#include <string.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

#define PROV_CONNECTED_BIT BIT0
#define PROV_FAILED_BIT BIT1

static const char *TAG = "wifi_prov";
static EventGroupHandle_t s_prov_event_group;

static void provisioning_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                                       void *event_data) {
    (void)arg;
    (void)event_data;
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV:
                ESP_LOGI(TAG, "Credentials received");
                break;
            case WIFI_PROV_CRED_FAIL:
                ESP_LOGW(TAG, "Provisioning failed");
                if (s_prov_event_group != NULL) {
                    xEventGroupSetBits(s_prov_event_group, PROV_FAILED_BIT);
                }
                break;
            case WIFI_PROV_END:
                ESP_LOGI(TAG, "Provisioning completed");
                break;
            default:
                break;
        }
    } else if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGW(TAG, "Provisioning STA disconnected");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Provisioning STA obtained IP address");
        if (s_prov_event_group != NULL) {
            xEventGroupSetBits(s_prov_event_group, PROV_CONNECTED_BIT);
        }
    }
}

static esp_err_t register_prov_handlers(void) {
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                                   provisioning_event_handler, NULL), TAG,
                        "wifi prov handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   provisioning_event_handler, NULL), TAG,
                        "wifi event handler");
    return esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, provisioning_event_handler, NULL);
}

static void unregister_prov_handlers(void) {
    esp_event_handler_unregister(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, provisioning_event_handler);
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, provisioning_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, provisioning_event_handler);
}

esp_err_t wifi_provisioner_acquire(const wifi_provisioner_config_t *cfg, wifi_sta_credentials_t *out) {
    if (cfg == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_prov_mgr_config_t prov_mgr_cfg = {
        .scheme = wifi_prov_scheme_softap,
        .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
    };
    ESP_RETURN_ON_ERROR(wifi_prov_mgr_init(prov_mgr_cfg), TAG, "init mgr");

    bool provisioned = false;
    ESP_RETURN_ON_ERROR(wifi_prov_mgr_is_provisioned(&provisioned), TAG, "is prov");

    wifi_config_t sta_cfg = { 0 };

    if (!provisioned) {
        if (s_prov_event_group == NULL) {
            s_prov_event_group = xEventGroupCreate();
        }
        if (s_prov_event_group == NULL) {
            wifi_prov_mgr_deinit();
            return ESP_ERR_NO_MEM;
        }
        xEventGroupClearBits(s_prov_event_group, PROV_CONNECTED_BIT | PROV_FAILED_BIT);
        esp_err_t err = register_prov_handlers();
        if (err != ESP_OK) {
            wifi_prov_mgr_deinit();
            return err;
        }

        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char service_name[33];
        const char *prefix = (cfg->service_prefix != NULL) ? cfg->service_prefix : "PROV";
        snprintf(service_name, sizeof(service_name), "%.*s%02X%02X", 16, prefix, mac[4], mac[5]);

        wifi_prov_scheme_softap_config_t ap_cfg = {
            .ssid = service_name,
            .password = (cfg->service_key != NULL) ? cfg->service_key : "",
            .channel = 1,
            .max_connection = 4,
        };
        wifi_prov_scheme_softap_set_config(&ap_cfg);

        wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
        wifi_prov_security1_params_t sec1 = {
            .pop = cfg->proof_of_possession,
        };
        const void *sec_params = &sec1;
        if (cfg->proof_of_possession == NULL || strlen(cfg->proof_of_possession) == 0) {
            security = WIFI_PROV_SECURITY_0;
            sec_params = NULL;
        }

        ESP_GOTO_ON_ERROR(wifi_prov_mgr_start_provisioning(security, sec_params, service_name,
                                                           ap_cfg.password), err, exit_start,
                          TAG, "start prov");

        TickType_t wait_ticks = (cfg->timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(cfg->timeout_ms);
        EventBits_t bits = xEventGroupWaitBits(s_prov_event_group, PROV_CONNECTED_BIT | PROV_FAILED_BIT,
                                               pdTRUE, pdFALSE, wait_ticks);

        if ((bits & PROV_CONNECTED_BIT) == 0) {
            if (bits & PROV_FAILED_BIT) {
                err = ESP_FAIL;
            } else {
                err = ESP_ERR_TIMEOUT;
            }
            wifi_prov_mgr_stop_provisioning();
            wifi_prov_mgr_deinit();
            unregister_prov_handlers();
            return err;
        }

        ESP_GOTO_ON_ERROR(wifi_prov_mgr_get_wifi_sta_config(&sta_cfg), err, exit_wait, TAG,
                          "get cfg");

    exit_wait:
        wifi_prov_mgr_stop_provisioning();
    exit_start:
        wifi_prov_mgr_deinit();
        unregister_prov_handlers();
        if (err != ESP_OK) {
            return err;
        }
    } else {
        ESP_RETURN_ON_ERROR(wifi_prov_mgr_get_wifi_sta_config(&sta_cfg), TAG, "get cfg");
        wifi_prov_mgr_deinit();
    }

    strlcpy(out->ssid, (const char *)sta_cfg.sta.ssid, sizeof(out->ssid));
    strlcpy(out->password, (const char *)sta_cfg.sta.password, sizeof(out->password));
    return ESP_OK;
}

void wifi_provisioner_shutdown(void) {
    if (s_prov_event_group != NULL) {
        vEventGroupDelete(s_prov_event_group);
        s_prov_event_group = NULL;
    }
}

