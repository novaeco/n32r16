#pragma once

#include "driver/i2c.h"
#include "esp_err.h"

esp_err_t i2c_bus_init(void);
esp_err_t i2c_bus_read(uint8_t addr, uint8_t *data, size_t len);
esp_err_t i2c_bus_write(uint8_t addr, const uint8_t *data, size_t len);
esp_err_t i2c_bus_write_read(uint8_t addr, const uint8_t *wr_data, size_t wr_len, uint8_t *rd_data,
                             size_t rd_len);
