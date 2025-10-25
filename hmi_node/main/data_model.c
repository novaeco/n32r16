#include "data_model.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "prefs_store.h"

#define HMI_MODEL_LOCK_TIMEOUT pdMS_TO_TICKS(1000)

static const char *TAG = "data_model";

static inline bool hmi_model_lock(hmi_data_model_t *model)
{
    return model->mutex && xSemaphoreTake(model->mutex, HMI_MODEL_LOCK_TIMEOUT) == pdTRUE;
}

static inline void hmi_model_unlock(hmi_data_model_t *model)
{
    if (model->mutex) {
        xSemaphoreGive(model->mutex);
    }
}

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

static void hmi_data_model_set_default_preferences(hmi_user_preferences_t *prefs)
{
    if (!prefs) {
        return;
    }
    prefs->dark_theme = true;
    prefs->use_fahrenheit = false;
    prefs->high_contrast = false;
    prefs->large_touch_targets = false;
    prefs->text_scale_percent = 100;
    prefs->language = HMI_LANGUAGE_EN;
    prefs->ssid[0] = '\0';
    prefs->password[0] = '\0';
    strncpy(prefs->mdns_target, "sensor-node.local", sizeof(prefs->mdns_target) - 1);
    prefs->mdns_target[sizeof(prefs->mdns_target) - 1] = '\0';
    sanitize_preferences(prefs);
}

void hmi_data_model_init(hmi_data_model_t *model)
{
    memset(model, 0, sizeof(*model));
    model->mutex = xSemaphoreCreateMutexStatic(&model->mutex_storage);
    configASSERT(model->mutex != NULL);
    hmi_data_model_set_default_preferences(&model->preferences);
    model->last_crc_ok = true;

    esp_err_t err = hmi_prefs_store_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init preferences store: %s", esp_err_to_name(err));
        return;
    }

    hmi_user_preferences_t stored = {0};
    err = hmi_prefs_store_load(&stored);
    if (err == ESP_OK) {
        sanitize_preferences(&stored);
        model->preferences = stored;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No stored preferences, using defaults");
    } else {
        ESP_LOGW(TAG, "Failed to load preferences: %s", esp_err_to_name(err));
    }
}

void hmi_data_model_set_update(hmi_data_model_t *model, const proto_sensor_update_t *update)
{
    if (!model || !update) {
        return;
    }
    if (!hmi_model_lock(model)) {
        return;
    }
    model->last_update = *update;
    model->has_update = true;
    hmi_model_unlock(model);
}

bool hmi_data_model_get_update(hmi_data_model_t *model, proto_sensor_update_t *out)
{
    if (!model || !out) {
        return false;
    }
    if (!hmi_model_lock(model)) {
        return false;
    }
    if (!model->has_update) {
        hmi_model_unlock(model);
        return false;
    }
    *out = model->last_update;
    model->has_update = false;
    hmi_model_unlock(model);
    return true;
}

bool hmi_data_model_peek_update(hmi_data_model_t *model, proto_sensor_update_t *out)
{
    if (!model || !out) {
        return false;
    }
    if (!hmi_model_lock(model)) {
        return false;
    }
    *out = model->last_update;
    bool valid = model->last_update.sequence_id != 0;
    hmi_model_unlock(model);
    return valid;
}

void hmi_data_model_set_connected(hmi_data_model_t *model, bool connected)
{
    if (!model) {
        return;
    }
    if (!hmi_model_lock(model)) {
        return;
    }
    model->connected = connected;
    hmi_model_unlock(model);
}

bool hmi_data_model_is_connected(const hmi_data_model_t *model)
{
    if (!model) {
        return false;
    }
    hmi_data_model_t *mutable_model = (hmi_data_model_t *)model;
    if (!hmi_model_lock(mutable_model)) {
        return false;
    }
    bool connected = mutable_model->connected;
    hmi_model_unlock(mutable_model);
    return connected;
}

void hmi_data_model_set_crc_status(hmi_data_model_t *model, bool ok)
{
    if (!model) {
        return;
    }
    if (!hmi_model_lock(model)) {
        return;
    }
    model->last_crc_ok = ok;
    hmi_model_unlock(model);
}

bool hmi_data_model_get_crc_status(const hmi_data_model_t *model)
{
    if (!model) {
        return false;
    }
    hmi_data_model_t *mutable_model = (hmi_data_model_t *)model;
    if (!hmi_model_lock(mutable_model)) {
        return false;
    }
    bool ok = mutable_model->last_crc_ok;
    hmi_model_unlock(mutable_model);
    return ok;
}

void hmi_data_model_get_preferences(const hmi_data_model_t *model, hmi_user_preferences_t *out)
{
    if (!model || !out) {
        return;
    }
    hmi_data_model_t *mutable_model = (hmi_data_model_t *)model;
    if (!hmi_model_lock(mutable_model)) {
        return;
    }
    *out = mutable_model->preferences;
    hmi_model_unlock(mutable_model);
}

void hmi_data_model_set_preferences(hmi_data_model_t *model, const hmi_user_preferences_t *prefs)
{
    if (!model || !prefs) {
        return;
    }
    if (!hmi_model_lock(model)) {
        return;
    }
    strncpy(model->preferences.ssid, prefs->ssid, sizeof(model->preferences.ssid) - 1);
    model->preferences.ssid[sizeof(model->preferences.ssid) - 1] = '\0';
    strncpy(model->preferences.password, prefs->password, sizeof(model->preferences.password) - 1);
    model->preferences.password[sizeof(model->preferences.password) - 1] = '\0';
    strncpy(model->preferences.mdns_target, prefs->mdns_target, sizeof(model->preferences.mdns_target) - 1);
    model->preferences.mdns_target[sizeof(model->preferences.mdns_target) - 1] = '\0';
    model->preferences.dark_theme = prefs->dark_theme;
    model->preferences.use_fahrenheit = prefs->use_fahrenheit;
    model->preferences.high_contrast = prefs->high_contrast;
    model->preferences.large_touch_targets = prefs->large_touch_targets;
    model->preferences.text_scale_percent = prefs->text_scale_percent;
    model->preferences.language = (prefs->language < HMI_LANGUAGE_MAX) ? prefs->language : HMI_LANGUAGE_EN;
    hmi_model_unlock(model);
}

void hmi_data_model_reset_preferences(hmi_data_model_t *model)
{
    if (!model) {
        return;
    }
    if (!hmi_model_lock(model)) {
        return;
    }
    hmi_data_model_set_default_preferences(&model->preferences);
    hmi_model_unlock(model);
}
