#pragma once

#include "common/proto/messages.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>

typedef struct {
    proto_sensor_update_t last_update;
    bool has_update;
    bool connected;
    SemaphoreHandle_t mutex;
    StaticSemaphore_t mutex_storage;
} hmi_data_model_t;

void hmi_data_model_init(hmi_data_model_t *model);
void hmi_data_model_set_update(hmi_data_model_t *model, const proto_sensor_update_t *update);
bool hmi_data_model_get_update(hmi_data_model_t *model, proto_sensor_update_t *out);
void hmi_data_model_set_connected(hmi_data_model_t *model, bool connected);
bool hmi_data_model_is_connected(const hmi_data_model_t *model);
