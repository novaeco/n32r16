#pragma once

#include "data_model.h"
#include <stdbool.h>
#include <stdint.h>

void io_task_start(sensor_data_model_t *model);
bool io_task_set_pwm(uint8_t channel, uint16_t duty);
bool io_task_set_pwm_frequency(uint16_t frequency_hz);
bool io_task_write_gpio(uint8_t device_index, uint16_t mask, uint16_t value);
