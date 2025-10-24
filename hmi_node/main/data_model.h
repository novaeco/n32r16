#pragma once

#include "common/proto/messages.h"
#include <stdbool.h>

typedef struct {
    proto_sensor_update_t last_update;
    bool has_update;
    bool connected;
} hmi_data_model_t;

void hmi_data_model_init(hmi_data_model_t *model);
void hmi_data_model_set_update(hmi_data_model_t *model, const proto_sensor_update_t *update);
const proto_sensor_update_t *hmi_data_model_get_update(const hmi_data_model_t *model);
void hmi_data_model_set_connected(hmi_data_model_t *model, bool connected);
bool hmi_data_model_is_connected(const hmi_data_model_t *model);
