#include "i2c_mock.h"

#include "i2c_bus.h"
#include "unity.h"
#include <string.h>

static i2c_mock_op_t s_ops[16];
static size_t s_op_count;
static size_t s_op_index;

void i2c_mock_set_sequence(const i2c_mock_op_t *ops, size_t count)
{
    size_t capacity = sizeof(s_ops) / sizeof(s_ops[0]);
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(capacity, count, "Too many I2C mock operations");
    for (size_t i = 0; i < count; ++i) {
        s_ops[i] = ops[i];
    }
    s_op_count = count;
    s_op_index = 0;
}

void i2c_mock_reset(void)
{
    s_op_count = 0;
    s_op_index = 0;
}

void i2c_mock_assert_complete(void)
{
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(s_op_count, s_op_index, "Not all I2C operations consumed");
}

static const i2c_mock_op_t *next_op(i2c_mock_op_type_t type, uint8_t addr)
{
    TEST_ASSERT_TRUE_MESSAGE(s_op_index < s_op_count, "I2C mock sequence exhausted");
    const i2c_mock_op_t *op = &s_ops[s_op_index++];
    TEST_ASSERT_EQUAL_UINT32(type, op->type);
    TEST_ASSERT_EQUAL_UINT8(addr, op->address);
    return op;
}

esp_err_t i2c_bus_write(uint8_t addr, const uint8_t *data, size_t len)
{
    const i2c_mock_op_t *op = next_op(I2C_MOCK_WRITE, addr);
    TEST_ASSERT_EQUAL_size_t(op->write_len, len);
    if (len > 0) {
        TEST_ASSERT_NOT_NULL(data);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(op->write_data, data, len);
    }
    return op->result;
}

esp_err_t i2c_bus_read(uint8_t addr, uint8_t *data, size_t len)
{
    const i2c_mock_op_t *op = next_op(I2C_MOCK_READ, addr);
    TEST_ASSERT_EQUAL_size_t(op->read_len, len);
    if (len > 0) {
        TEST_ASSERT_NOT_NULL(data);
        memcpy(data, op->read_data, len);
    }
    return op->result;
}

esp_err_t i2c_bus_write_read(uint8_t addr, const uint8_t *wr_data, size_t wr_len, uint8_t *rd_data,
                             size_t rd_len)
{
    const i2c_mock_op_t *op = next_op(I2C_MOCK_WRITE_READ, addr);
    TEST_ASSERT_EQUAL_size_t(op->write_len, wr_len);
    if (wr_len > 0) {
        TEST_ASSERT_NOT_NULL(wr_data);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(op->write_data, wr_data, wr_len);
    }
    TEST_ASSERT_EQUAL_size_t(op->read_len, rd_len);
    if (rd_len > 0) {
        TEST_ASSERT_NOT_NULL(rd_data);
        memcpy(rd_data, op->read_data, rd_len);
    }
    return op->result;
}

esp_err_t i2c_bus_init(void)
{
    return ESP_OK;
}
