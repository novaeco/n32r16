#pragma once

#include "esp_err.h"
#include "onewire_bus.h"
#include <stddef.h>
#include <stdint.h>

uint32_t ds18b20_conversion_time_ms(uint8_t resolution_bits);
esp_err_t ds18b20_start_conversion(onewire_bus_handle_t bus, const onewire_device_t *devices,
                                   size_t count, uint8_t resolution_bits);
esp_err_t ds18b20_check_conversion(onewire_bus_handle_t bus, bool *ready);
esp_err_t ds18b20_read_temperature(onewire_bus_handle_t bus, const onewire_device_t *device,
                                   float *temperature_c);
