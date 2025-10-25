#pragma once

#include "common/proto/messages.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>

typedef struct {
    char ssid[33];
    char password[65];
    char mdns_target[64];
    bool dark_theme;
    bool use_fahrenheit;
} hmi_user_preferences_t;

typedef struct {
    proto_sensor_update_t last_update;
    bool has_update;
    bool connected;
    bool last_crc_ok;
    hmi_user_preferences_t preferences;
    SemaphoreHandle_t mutex;
    StaticSemaphore_t mutex_storage;
} hmi_data_model_t;

void hmi_data_model_init(hmi_data_model_t *model);
void hmi_data_model_set_update(hmi_data_model_t *model, const proto_sensor_update_t *update);
bool hmi_data_model_get_update(hmi_data_model_t *model, proto_sensor_update_t *out);
bool hmi_data_model_peek_update(hmi_data_model_t *model, proto_sensor_update_t *out);
void hmi_data_model_set_connected(hmi_data_model_t *model, bool connected);
bool hmi_data_model_is_connected(const hmi_data_model_t *model);
void hmi_data_model_set_crc_status(hmi_data_model_t *model, bool ok);
bool hmi_data_model_get_crc_status(const hmi_data_model_t *model);
void hmi_data_model_get_preferences(const hmi_data_model_t *model, hmi_user_preferences_t *out);
void hmi_data_model_set_preferences(hmi_data_model_t *model, const hmi_user_preferences_t *prefs);
void hmi_data_model_reset_preferences(hmi_data_model_t *model);
