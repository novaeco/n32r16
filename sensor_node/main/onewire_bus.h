#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"

#define ONEWIRE_MAX_DEVICES 8

typedef struct {
    uint64_t roms[ONEWIRE_MAX_DEVICES];
    size_t count;
} onewire_device_list_t;

esp_err_t onewire_bus_init(gpio_num_t gpio);
bool onewire_bus_reset(void);
bool onewire_bus_read(uint8_t *buffer, size_t len);
bool onewire_bus_write(const uint8_t *buffer, size_t len);
bool onewire_bus_search(onewire_device_list_t *list);
uint32_t onewire_bus_get_error_count(void);

