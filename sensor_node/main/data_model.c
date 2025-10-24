#include "data_model.h"

#include "common/proto/messages.h"
#include <math.h>
#include <string.h>

void data_model_init(sensor_data_model_t *model)
{
    memset(model, 0, sizeof(*model));
    model->current.pwm.frequency_hz = 500;
    model->initialized = true;
}

void data_model_set_sht20(sensor_data_model_t *model, size_t index, const char *id, float temp,
                          float humidity)
{
    if (index >= 2) {
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
}

void data_model_set_ds18b20(sensor_data_model_t *model, size_t index, const onewire_device_t *device,
                            float temp)
{
    if (index >= 4 || !device) {
        return;
    }
    proto_ds18b20_reading_t *entry = &model->current.ds18b20[index];
    memcpy(entry->rom_code, device->rom_code, sizeof(entry->rom_code));
    entry->temperature_c = temp;
    if (index + 1 > model->current.ds18b20_count) {
        model->current.ds18b20_count = index + 1;
    }
}

void data_model_set_gpio(sensor_data_model_t *model, size_t dev_index, uint16_t porta, uint16_t portb)
{
    if (dev_index >= 2) {
        return;
    }
    model->current.mcp[dev_index].port_a = porta;
    model->current.mcp[dev_index].port_b = portb;
}

void data_model_set_pwm(sensor_data_model_t *model, const uint16_t *duty, size_t count,
                        uint16_t frequency_hz)
{
    if (!duty) {
        return;
    }
    size_t max = count < 16 ? count : 16;
    for (size_t i = 0; i < max; ++i) {
        model->current.pwm.duty_cycle[i] = duty[i];
    }
    model->current.pwm.frequency_hz = frequency_hz;
}

void data_model_set_timestamp(sensor_data_model_t *model, uint32_t timestamp_ms)
{
    model->current.timestamp_ms = timestamp_ms;
}

void data_model_increment_seq(sensor_data_model_t *model)
{
    model->current.sequence_id++;
}

static bool value_changed(float a, float b, float threshold)
{
    return fabsf(a - b) > threshold;
}

bool data_model_should_publish(sensor_data_model_t *model, float temp_threshold, float humidity_threshold)
{
    if (!model->initialized) {
        return false;
    }
    if (model->last_published.sequence_id == 0) {
        return true;
    }
    for (size_t i = 0; i < model->current.sht20_count; ++i) {
        if (value_changed(model->current.sht20[i].temperature_c, model->last_published.sht20[i].temperature_c,
                          temp_threshold) ||
            value_changed(model->current.sht20[i].humidity_percent,
                          model->last_published.sht20[i].humidity_percent, humidity_threshold)) {
            return true;
        }
    }
    for (size_t i = 0; i < model->current.ds18b20_count; ++i) {
        if (value_changed(model->current.ds18b20[i].temperature_c,
                          model->last_published.ds18b20[i].temperature_c, temp_threshold)) {
            return true;
        }
    }
    if (memcmp(model->current.mcp, model->last_published.mcp, sizeof(model->current.mcp)) != 0) {
        return true;
    }
    if (memcmp(model->current.pwm.duty_cycle, model->last_published.pwm.duty_cycle,
               sizeof(model->current.pwm.duty_cycle)) != 0 ||
        model->current.pwm.frequency_hz != model->last_published.pwm.frequency_hz) {
        return true;
    }
    return false;
}

bool data_model_build(sensor_data_model_t *model, bool use_cbor, uint8_t **out_buf, size_t *out_len,
                      uint32_t *crc32)
{
    if (!model->initialized) {
        return false;
    }
    bool ok = proto_encode_sensor_update(&model->current, use_cbor, out_buf, out_len, crc32);
    if (ok) {
        model->last_published = model->current;
    }
    return ok;
}
