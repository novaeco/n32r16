#include "data_model.h"

#include <string.h>

void hmi_data_model_init(hmi_data_model_t *model)
{
    memset(model, 0, sizeof(*model));
}

void hmi_data_model_set_update(hmi_data_model_t *model, const proto_sensor_update_t *update)
{
    if (!model || !update) {
        return;
    }
    model->last_update = *update;
    model->has_update = true;
}

const proto_sensor_update_t *hmi_data_model_get_update(const hmi_data_model_t *model)
{
    if (!model || !model->has_update) {
        return NULL;
    }
    return &model->last_update;
}

void hmi_data_model_set_connected(hmi_data_model_t *model, bool connected)
{
    if (!model) {
        return;
    }
    model->connected = connected;
}

bool hmi_data_model_is_connected(const hmi_data_model_t *model)
{
    return model ? model->connected : false;
}
