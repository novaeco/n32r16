#include "prefs_store.h"

#include <stdbool.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_flash_secure.h"

#define PREFS_NAMESPACE "hmi_prefs"
#define PREFS_KEY "user"

static const char *TAG = "prefs_store";
static bool s_secure_nvs_ready;

static void sanitize_preferences(hmi_user_preferences_t *prefs)
{
    if (!prefs) {
        return;
    }
    prefs->ssid[sizeof(prefs->ssid) - 1] = '\0';
    prefs->password[sizeof(prefs->password) - 1] = '\0';
    prefs->mdns_target[sizeof(prefs->mdns_target) - 1] = '\0';
    if (prefs->text_scale_percent < 80 || prefs->text_scale_percent > 140) {
        prefs->text_scale_percent = 100;
    }
    if (prefs->language >= HMI_LANGUAGE_MAX) {
        prefs->language = HMI_LANGUAGE_EN;
    }
}

static esp_err_t ensure_secure_nvs(void)
{
    if (s_secure_nvs_ready) {
        return ESP_OK;
    }

    nvs_sec_cfg_t cfg = {0};
    esp_err_t err = nvs_flash_secure_init(&cfg);
    if (err == ESP_ERR_NVS_SEC_KEY_PART_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS key partition missing, generating");
        ESP_RETURN_ON_ERROR(nvs_flash_generate_keys(&cfg), TAG, "failed to generate NVS keys");
        err = nvs_flash_secure_init(&cfg);
    }
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition invalid, erasing");
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_deinit());
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "failed to erase NVS");
        err = nvs_flash_secure_init(&cfg);
    }
    ESP_RETURN_ON_ERROR(err, TAG, "secure init failed");
    ESP_RETURN_ON_ERROR(nvs_flash_secure_set_flash(&cfg), TAG, "secure flash setup failed");

    s_secure_nvs_ready = true;
    return ESP_OK;
}

esp_err_t hmi_prefs_store_init(void)
{
    return ensure_secure_nvs();
}

esp_err_t hmi_prefs_store_load(hmi_user_preferences_t *prefs)
{
    if (!prefs) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(prefs, 0, sizeof(*prefs));

    ESP_RETURN_ON_ERROR(ensure_secure_nvs(), TAG, "secure NVS unavailable");

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open_from_partition("nvs", PREFS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t required = sizeof(*prefs);
    err = nvs_get_blob(handle, PREFS_KEY, prefs, &required);
    nvs_close(handle);
    if (err == ESP_OK) {
        if (required != sizeof(*prefs)) {
            ESP_LOGW(TAG, "Unexpected preferences blob length=%zu", required);
            return ESP_ERR_INVALID_SIZE;
        }
        sanitize_preferences(prefs);
    }
    return err;
}

esp_err_t hmi_prefs_store_save(const hmi_user_preferences_t *prefs)
{
    if (!prefs) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(ensure_secure_nvs(), TAG, "secure NVS unavailable");

    hmi_user_preferences_t tmp = *prefs;
    sanitize_preferences(&tmp);

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open_from_partition("nvs", PREFS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, PREFS_KEY, &tmp, sizeof(tmp));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t hmi_prefs_store_erase(void)
{
    ESP_RETURN_ON_ERROR(ensure_secure_nvs(), TAG, "secure NVS unavailable");

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open_from_partition("nvs", PREFS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(handle, PREFS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
