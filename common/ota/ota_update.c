#include "ota_update.h"

#include "esp_https_ota.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char *TAG = "ota_update";

static void ota_task(void *arg)
{
    ota_update_config_t *cfg = (ota_update_config_t *)arg;
    if (!cfg) {
        vTaskDelete(NULL);
        return;
    }

    if (cfg->check_delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(cfg->check_delay_ms));
    }

    if (cfg->wait_for_connectivity) {
        while (!cfg->wait_for_connectivity(5000)) {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    esp_http_client_config_t http_cfg = {
        .url = cfg->url,
        .cert_pem = (const char *)cfg->cert_pem,
        .cert_len = cfg->cert_len,
        .timeout_ms = 10000,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    ESP_LOGI(TAG, "Starting OTA from %s", cfg->url);
    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA successful");
        if (cfg->auto_reboot) {
            ESP_LOGI(TAG, "Rebooting to new firmware");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    }

    free(cfg);
    vTaskDelete(NULL);
}

esp_err_t ota_update_schedule(const ota_update_config_t *config)
{
    if (!config || !config->url) {
        return ESP_ERR_INVALID_ARG;
    }
    ota_update_config_t *copy = (ota_update_config_t *)malloc(sizeof(*copy));
    if (!copy) {
        return ESP_ERR_NO_MEM;
    }
    *copy = *config;
    if (copy->check_delay_ms == 0) {
        copy->check_delay_ms = 1000;
    }
    BaseType_t created = xTaskCreate(ota_task, "ota_task", 8192, copy, 4, NULL);
    if (created != pdPASS) {
        free(copy);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
