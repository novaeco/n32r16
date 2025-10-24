#include "data_model.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define HMI_MODEL_LOCK_TIMEOUT pdMS_TO_TICKS(1000)

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

void hmi_data_model_init(hmi_data_model_t *model)
{
    memset(model, 0, sizeof(*model));
    model->mutex = xSemaphoreCreateMutexStatic(&model->mutex_storage);
    configASSERT(model->mutex != NULL);
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
    hmi_model_unlock(model);
    return true;
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
