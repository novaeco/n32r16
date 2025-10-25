#include "ota_update.h"

#include "esp_https_ota.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ota_update";

#define OTA_DEFAULT_CHECK_DELAY_MS (1000U)
#define OTA_DEFAULT_INITIAL_BACKOFF_MS (5000U)
#define OTA_DEFAULT_MAX_BACKOFF_MS (60000U)
#define OTA_DEFAULT_MAX_RETRIES (5U)
#define OTA_DEFAULT_NAMESPACE "ota_state"
#define OTA_DEFAULT_VERSION_KEY "last_version"

static const char *ota_get_namespace(const ota_update_config_t *cfg)
{
    return cfg->nvs_namespace ? cfg->nvs_namespace : OTA_DEFAULT_NAMESPACE;
}

static const char *ota_get_version_key(const ota_update_config_t *cfg)
{
    return cfg->nvs_key ? cfg->nvs_key : OTA_DEFAULT_VERSION_KEY;
}

static esp_err_t ota_load_last_version(const ota_update_config_t *cfg, char *buffer, size_t buffer_len, bool *found)
{
    if (!buffer || buffer_len == 0 || !found) {
        return ESP_ERR_INVALID_ARG;
    }

    *found = false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ota_get_namespace(cfg), NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t required = buffer_len;
    err = nvs_get_str(handle, ota_get_version_key(cfg), buffer, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    } else if (err == ESP_OK) {
        *found = true;
    }
    nvs_close(handle);
    return err;
}

static esp_err_t ota_store_last_version(const ota_update_config_t *cfg, const char *version)
{
    if (!version) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ota_get_namespace(cfg), NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, ota_get_version_key(cfg), version);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static bool ota_get_running_app_desc(esp_app_desc_t *out_desc)
{
    if (!out_desc) {
        return false;
    }
    memset(out_desc, 0, sizeof(*out_desc));
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        return false;
    }
    if (esp_ota_get_partition_description(running, out_desc) != ESP_OK) {
        return false;
    }
    return true;
}

static esp_err_t ota_perform_update(const ota_update_config_t *cfg, const esp_app_desc_t *running_app,
                                    const char *stored_version, esp_app_desc_t *applied_desc, bool *should_retry)
{
    if (applied_desc) {
        memset(applied_desc, 0, sizeof(*applied_desc));
    }
    if (should_retry) {
        *should_retry = true;
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

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_app_desc_t new_desc = {0};
    err = esp_https_ota_get_img_desc(handle, &new_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read OTA descriptor: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        return err;
    }

    if (cfg->validate) {
        const uint8_t *sha = (const uint8_t *)new_desc.app_elf_sha256;
        err = cfg->validate(&new_desc, running_app, sha, stored_version, cfg->validate_ctx);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "OTA validation rejected image %s: %s", new_desc.version, esp_err_to_name(err));
            if (should_retry) {
                *should_retry = false;
            }
            esp_https_ota_abort(handle);
            return err;
        }
    }

    do {
        err = esp_https_ota_perform(handle);
        if (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    } while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA transfer failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        return err;
    }

    if (!esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGE(TAG, "OTA transfer incomplete");
        esp_https_ota_abort(handle);
        return ESP_ERR_INVALID_RESPONSE;
    }

    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
        if (should_retry) {
            *should_retry = (err != ESP_ERR_OTA_VALIDATE_FAILED);
        }
        return err;
    }

    ESP_LOGI(TAG, "Downloaded image %s", new_desc.version);
    if (applied_desc) {
        *applied_desc = new_desc;
    }
    if (should_retry) {
        *should_retry = false;
    }
    return ESP_OK;
}

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

    esp_err_t mark_err = esp_ota_mark_app_valid_cancel_rollback();
    if (mark_err == ESP_OK) {
        ESP_LOGI(TAG, "Marked running firmware valid");
    } else if (mark_err != ESP_ERR_OTA_ROLLBACK_DISABLED && mark_err != ESP_ERR_OTA_PARTITION_CONFLICT) {
        ESP_LOGW(TAG, "Failed to mark app valid: %s", esp_err_to_name(mark_err));
    }

    if (cfg->wait_for_connectivity) {
        while (!cfg->wait_for_connectivity(5000)) {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    esp_app_desc_t running_desc = {0};
    bool has_running = ota_get_running_app_desc(&running_desc);

    char stored_version[sizeof(((esp_app_desc_t *)0)->version) + 1] = {0};
    bool has_stored_version = false;
    esp_err_t nvs_err = ota_load_last_version(cfg, stored_version, sizeof(stored_version), &has_stored_version);
    if (nvs_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load stored OTA version: %s", esp_err_to_name(nvs_err));
        has_stored_version = false;
    }

    uint32_t max_attempts = cfg->max_retries ? cfg->max_retries : OTA_DEFAULT_MAX_RETRIES;
    uint32_t backoff_ms = cfg->initial_backoff_ms ? cfg->initial_backoff_ms : OTA_DEFAULT_INITIAL_BACKOFF_MS;
    uint32_t max_backoff_ms = cfg->max_backoff_ms ? cfg->max_backoff_ms : OTA_DEFAULT_MAX_BACKOFF_MS;
    esp_err_t last_err = ESP_FAIL;

    for (uint32_t attempt = 1; attempt <= max_attempts; ++attempt) {
        bool should_retry = true;
        esp_app_desc_t applied_desc = {0};
        const esp_app_desc_t *running_ptr = has_running ? &running_desc : NULL;
        const char *stored_ptr = has_stored_version ? stored_version : NULL;
        ESP_LOGI(TAG, "Starting OTA attempt %u/%u from %s", attempt, max_attempts, cfg->url);
        last_err = ota_perform_update(cfg, running_ptr, stored_ptr, &applied_desc, &should_retry);
        if (last_err == ESP_OK) {
            esp_err_t store_err = ota_store_last_version(cfg, applied_desc.version);
            if (store_err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to persist OTA version %s: %s", applied_desc.version, esp_err_to_name(store_err));
            } else {
                strncpy(stored_version, applied_desc.version, sizeof(stored_version));
                stored_version[sizeof(stored_version) - 1] = '\0';
                has_stored_version = true;
            }

            ESP_LOGI(TAG, "OTA successful");
            if (cfg->auto_reboot) {
                ESP_LOGI(TAG, "Rebooting to new firmware");
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_restart();
            }
            free(cfg);
            vTaskDelete(NULL);
            return;
        }

        if (!should_retry) {
            ESP_LOGW(TAG, "OTA aborted without retry (err=%s)", esp_err_to_name(last_err));
            break;
        }

        if (attempt < max_attempts) {
            ESP_LOGW(TAG, "OTA attempt %u/%u failed (%s), backing off %ums", attempt, max_attempts,
                     esp_err_to_name(last_err), backoff_ms);
            if (backoff_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            }
            uint64_t next_backoff = (uint64_t)backoff_ms * 2ULL;
            if (next_backoff > max_backoff_ms) {
                backoff_ms = max_backoff_ms;
            } else if (next_backoff == 0ULL) {
                backoff_ms = max_backoff_ms;
            } else {
                backoff_ms = (uint32_t)next_backoff;
            }
        } else {
            ESP_LOGE(TAG, "OTA failed after %u attempts (%s)", max_attempts, esp_err_to_name(last_err));
        }
    }

    free(cfg);
    vTaskDelete(NULL);
}

void ota_update_task_entry(void *arg)
{
    ota_task(arg);
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
        copy->check_delay_ms = OTA_DEFAULT_CHECK_DELAY_MS;
    }
    if (copy->initial_backoff_ms == 0) {
        copy->initial_backoff_ms = OTA_DEFAULT_INITIAL_BACKOFF_MS;
    }
    if (copy->max_backoff_ms == 0) {
        copy->max_backoff_ms = OTA_DEFAULT_MAX_BACKOFF_MS;
    }
    if (copy->max_retries == 0) {
        copy->max_retries = OTA_DEFAULT_MAX_RETRIES;
    }
    BaseType_t created = xTaskCreate(ota_task, "ota_task", 8192, copy, 4, NULL);
    if (created != pdPASS) {
        free(copy);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
