#pragma once

#include "driver/i2c.h"

esp_err_t i2c_bus_init(i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t frequency_hz);
esp_err_t i2c_bus_write_read(i2c_port_t port, uint8_t addr, const uint8_t *write_data,
                             size_t write_len, uint8_t *read_data, size_t read_len);
uint32_t i2c_bus_get_error_count(void);

