#pragma once

#include "driver/gpio.h"
#include "esp_err.h"
#include "onewire_bus.h"

esp_err_t onewire_bus_manager_init(gpio_num_t pin, onewire_bus_handle_t *out_handle);
esp_err_t onewire_bus_scan(onewire_bus_handle_t bus, onewire_device_t *devices, size_t max_devices,
                           size_t *found);
