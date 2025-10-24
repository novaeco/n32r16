#include "data_model.h"

#include "common/proto/messages.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>
#include <string.h>

#define DATA_MODEL_LOCK_TIMEOUT pdMS_TO_TICKS(1000)

static inline bool data_model_lock(sensor_data_model_t *model)
{
    return model->mutex && xSemaphoreTake(model->mutex, DATA_MODEL_LOCK_TIMEOUT) == pdTRUE;
}

static inline void data_model_unlock(sensor_data_model_t *model)
{
    if (model->mutex) {
        xSemaphoreGive(model->mutex);
    }
}

void data_model_init(sensor_data_model_t *model)
{
    memset(model, 0, sizeof(*model));
    model->mutex = xSemaphoreCreateMutexStatic(&model->mutex_storage);
    configASSERT(model->mutex != NULL);
    model->current.pwm.frequency_hz = 500;
    model->initialized = true;
}

void data_model_set_sht20(sensor_data_model_t *model, size_t index, const char *id, float temp,
                          float humidity)
{
    if (!model || !model->initialized || index >= 2) {
        return;
    }
    if (!data_model_lock(model)) {
        return;
    }
    proto_sht20_reading_t *entry = &model->current.sht20[index];
    strncpy(entry->id, id, sizeof(entry->id) - 1);
    entry->id[sizeof(entry->id) - 1] = '\0';
    entry->temperature_c = temp;
    entry->humidity_percent = humidity;
    if (index + 1 > model->current.sht20_count) {
        model->current.sht20_count = index + 1;
    }
    data_model_unlock(model);
}

void data_model_set_ds18b20(sensor_data_model_t *model, size_t index, const onewire_device_t *device,
                            float temp)
{
    if (!model || !model->initialized || index >= 4 || !device) {
        return;
    }
    if (!data_model_lock(model)) {
        return;
    }
    proto_ds18b20_reading_t *entry = &model->current.ds18b20[index];
    memcpy(entry->rom_code, device->rom_code, sizeof(entry->rom_code));
    entry->temperature_c = temp;
    if (index + 1 > model->current.ds18b20_count) {
        model->current.ds18b20_count = index + 1;
    }
    data_model_unlock(model);
}

void data_model_set_gpio(sensor_data_model_t *model, size_t dev_index, uint16_t porta, uint16_t portb)
{
    if (!model || !model->initialized || dev_index >= 2) {
        return;
    }
    if (!data_model_lock(model)) {
        return;
    }
    model->current.mcp[dev_index].port_a = porta;
    model->current.mcp[dev_index].port_b = portb;
    data_model_unlock(model);
}

void data_model_set_pwm(sensor_data_model_t *model, const uint16_t *duty, size_t count,
                        uint16_t frequency_hz)
{
    if (!model || !model->initialized || !duty) {
        return;
    }
    if (!data_model_lock(model)) {
        return;
    }
    size_t max = count < 16 ? count : 16;
    for (size_t i = 0; i < max; ++i) {
        model->current.pwm.duty_cycle[i] = duty[i];
    }
    model->current.pwm.frequency_hz = frequency_hz;
    data_model_unlock(model);
}

void data_model_set_timestamp(sensor_data_model_t *model, uint32_t timestamp_ms)
{
    if (!model || !model->initialized) {
        return;
    }
    if (!data_model_lock(model)) {
        return;
    }
    model->current.timestamp_ms = timestamp_ms;
    data_model_unlock(model);
}

void data_model_increment_seq(sensor_data_model_t *model)
{
    if (!model || !model->initialized) {
        return;
    }
    if (!data_model_lock(model)) {
        return;
    }
    model->current.sequence_id++;
    data_model_unlock(model);
}

static bool value_changed(float a, float b, float threshold)
{
    return fabsf(a - b) > threshold;
}

bool data_model_should_publish(sensor_data_model_t *model, float temp_threshold, float humidity_threshold)
{
    if (!model || !model->initialized) {
        return false;
    }
    if (!data_model_lock(model)) {
        return false;
    }
    if (model->last_published.sequence_id == 0) {
        data_model_unlock(model);
        return true;
    }
    for (size_t i = 0; i < model->current.sht20_count; ++i) {
        if (value_changed(model->current.sht20[i].temperature_c, model->last_published.sht20[i].temperature_c,
                          temp_threshold) ||
            value_changed(model->current.sht20[i].humidity_percent,
                          model->last_published.sht20[i].humidity_percent, humidity_threshold)) {
            data_model_unlock(model);
            return true;
        }
    }
    for (size_t i = 0; i < model->current.ds18b20_count; ++i) {
        if (value_changed(model->current.ds18b20[i].temperature_c,
                          model->last_published.ds18b20[i].temperature_c, temp_threshold)) {
            data_model_unlock(model);
            return true;
        }
    }
    if (memcmp(model->current.mcp, model->last_published.mcp, sizeof(model->current.mcp)) != 0) {
        data_model_unlock(model);
        return true;
    }
    if (memcmp(model->current.pwm.duty_cycle, model->last_published.pwm.duty_cycle,
               sizeof(model->current.pwm.duty_cycle)) != 0 ||
        model->current.pwm.frequency_hz != model->last_published.pwm.frequency_hz) {
        data_model_unlock(model);
        return true;
    }
    data_model_unlock(model);
    return false;
}

bool data_model_build(sensor_data_model_t *model, bool use_cbor, uint8_t **out_buf, size_t *out_len,
                      uint32_t *crc32)
{
    if (!model || !out_buf || !out_len || !model->initialized) {
        return false;
    }
    if (!data_model_lock(model)) {
        return false;
    }
    size_t index = model->next_encode_index % SENSOR_DATA_MODEL_BUFFER_COUNT;
    uint8_t *buffer = model->encode_buffers[index];
    size_t capacity = SENSOR_DATA_MODEL_MAX_MESSAGE_SIZE;
    uint32_t local_crc = 0;
    bool ok = proto_encode_sensor_update_into(&model->current, use_cbor, buffer, &capacity, &local_crc);
    if (ok) {
        model->last_published = model->current;
        model->encode_lengths[index] = capacity;
        model->next_encode_index = (index + 1) % SENSOR_DATA_MODEL_BUFFER_COUNT;
    }
    data_model_unlock(model);
    if (!ok) {
        return false;
    }
    *out_buf = buffer;
    *out_len = capacity;
    if (crc32) {
        *crc32 = local_crc;
    }
    return true;
}
