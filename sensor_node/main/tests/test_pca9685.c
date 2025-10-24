#include "drivers/pca9685.h"

#include "i2c_mock.h"
#include "unity.h"

TEST_CASE("pca9685 initialises frequency", "[pca9685]")
{
    const uint8_t addr = 0x41;
    i2c_mock_op_t ops[] = {
        {I2C_MOCK_WRITE, addr, {0x00, 0x10}, 2, {0}, 0, ESP_OK},
        {I2C_MOCK_WRITE, addr, {0xFE, 0x0B}, 2, {0}, 0, ESP_OK},
        {I2C_MOCK_WRITE, addr, {0x00, 0xA1}, 2, {0}, 0, ESP_OK},
        {I2C_MOCK_WRITE, addr, {0x01, 0x04}, 2, {0}, 0, ESP_OK},
    };
    i2c_mock_set_sequence(ops, sizeof(ops) / sizeof(ops[0]));
    TEST_ASSERT_EQUAL(ESP_OK, pca9685_init(addr, 500));
    i2c_mock_assert_complete();
}

TEST_CASE("pca9685 sets pwm duty", "[pca9685]")
{
    const uint8_t addr = 0x41;
    i2c_mock_op_t ops[] = {
        {I2C_MOCK_WRITE, addr, {0x06 + 4 * 3, 0x00, 0x00, 0x00, 0x04}, 5, {0}, 0, ESP_OK},
    };
    i2c_mock_set_sequence(ops, sizeof(ops) / sizeof(ops[0]));
    TEST_ASSERT_EQUAL(ESP_OK, pca9685_set_pwm(addr, 3, 1024));
    i2c_mock_assert_complete();
}
