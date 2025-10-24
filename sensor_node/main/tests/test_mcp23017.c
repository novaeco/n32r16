#include "drivers/mcp23017.h"

#include "i2c_mock.h"
#include "unity.h"

TEST_CASE("mcp23017 configures directions and pullups", "[mcp23017]")
{
    const uint8_t addr = 0x20;
    i2c_mock_op_t ops[] = {
        {I2C_MOCK_WRITE, addr, {0x00, 0xFF, 0x00}, 3, {0}, 0, ESP_OK},
        {I2C_MOCK_WRITE, addr, {0x0C, 0xFF, 0xFF}, 3, {0}, 0, ESP_OK},
    };
    i2c_mock_set_sequence(ops, sizeof(ops) / sizeof(ops[0]));
    TEST_ASSERT_EQUAL(ESP_OK, mcp23017_init(addr, 0x00FF, 0xFFFF));
    i2c_mock_assert_complete();
}

TEST_CASE("mcp23017 write gpio applies mask", "[mcp23017]")
{
    const uint8_t addr = 0x20;
    i2c_mock_op_t ops[] = {
        {I2C_MOCK_WRITE_READ, addr, {0x14}, 1, {0xAA, 0x55}, 2, ESP_OK},
        {I2C_MOCK_WRITE, addr, {0x14, (uint8_t)((0xAA & ~0x0F) | 0x05), 0x55}, 3, {0}, 0, ESP_OK},
    };
    i2c_mock_set_sequence(ops, sizeof(ops) / sizeof(ops[0]));
    TEST_ASSERT_EQUAL(ESP_OK, mcp23017_write_gpio(addr, 0x000F, 0x0005));
    i2c_mock_assert_complete();
}
