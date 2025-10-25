#pragma once

#include "data_model.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hmi_prefs_store_init(void);
esp_err_t hmi_prefs_store_load(hmi_user_preferences_t *prefs);
esp_err_t hmi_prefs_store_save(const hmi_user_preferences_t *prefs);
esp_err_t hmi_prefs_store_erase(void);

#ifdef __cplusplus
}
#endif

