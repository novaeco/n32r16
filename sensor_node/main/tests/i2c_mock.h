#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    I2C_MOCK_WRITE,
    I2C_MOCK_READ,
    I2C_MOCK_WRITE_READ,
} i2c_mock_op_type_t;

typedef struct {
    i2c_mock_op_type_t type;
    uint8_t address;
    uint8_t write_data[8];
    size_t write_len;
    uint8_t read_data[8];
    size_t read_len;
    esp_err_t result;
} i2c_mock_op_t;

void i2c_mock_set_sequence(const i2c_mock_op_t *ops, size_t count);
void i2c_mock_reset(void);
void i2c_mock_assert_complete(void);
