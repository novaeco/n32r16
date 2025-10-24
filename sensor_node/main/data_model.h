#pragma once

#include "common/proto/messages.h"
#include "onewire_bus.h"
#include <stdbool.h>

typedef struct {
    proto_sensor_update_t current;
    proto_sensor_update_t last_published;
    bool initialized;
} sensor_data_model_t;

void data_model_init(sensor_data_model_t *model);
void data_model_set_sht20(sensor_data_model_t *model, size_t index, const char *id, float temp,
                          float humidity);
void data_model_set_ds18b20(sensor_data_model_t *model, size_t index, const onewire_device_t *device,
                            float temp);
void data_model_set_gpio(sensor_data_model_t *model, size_t dev_index, uint16_t porta, uint16_t portb);
void data_model_set_pwm(sensor_data_model_t *model, const uint16_t *duty, size_t count,
                        uint16_t frequency_hz);
void data_model_set_timestamp(sensor_data_model_t *model, uint32_t timestamp_ms);
bool data_model_should_publish(sensor_data_model_t *model, float temp_threshold, float humidity_threshold);
bool data_model_build(sensor_data_model_t *model, bool use_cbor, uint8_t **out_buf, size_t *out_len,
                      uint32_t *crc32);
void data_model_increment_seq(sensor_data_model_t *model);
