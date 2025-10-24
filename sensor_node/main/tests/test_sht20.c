#include "drivers/sht20.h"

#include "i2c_mock.h"
#include "unity.h"
#include <math.h>

static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if (crc & 0x80U) {
                crc = (uint8_t)((crc << 1U) ^ 0x131U);
            } else {
                crc <<= 1U;
            }
        }
    }
    return crc;
}

TEST_CASE("sht20 retries and averages data", "[sht20]")
{
    float temperature = 0.0f;
    float humidity = 0.0f;
    const uint8_t addr = 0x40;

    uint16_t temp_raw = (uint16_t)lrintf(((23.5f + 46.85f) / 175.72f) * 65536.0f);
    uint8_t temp_bytes[2] = {(uint8_t)(temp_raw >> 8), (uint8_t)(temp_raw & 0xFF)};
    uint8_t temp_crc = crc8(temp_bytes, 2);

    uint16_t hum_raw = (uint16_t)lrintf(((52.1f + 6.0f) / 125.0f) * 65536.0f);
    uint8_t hum_bytes[2] = {(uint8_t)(hum_raw >> 8), (uint8_t)(hum_raw & 0xFF)};
    uint8_t hum_crc = crc8(hum_bytes, 2);

    const uint8_t cmd_temp = 0xF3;
    const uint8_t cmd_hum = 0xF5;
    i2c_mock_op_t ops[] = {
        {I2C_MOCK_WRITE, addr, {cmd_temp}, 1, {0}, 0, ESP_OK},
        {I2C_MOCK_READ, addr, {0}, 0, {temp_bytes[0], temp_bytes[1], (uint8_t)(temp_crc ^ 0xFF)}, 3, ESP_OK},
        {I2C_MOCK_WRITE, addr, {cmd_temp}, 1, {0}, 0, ESP_OK},
        {I2C_MOCK_READ, addr, {0}, 0, {temp_bytes[0], temp_bytes[1], temp_crc}, 3, ESP_OK},
        {I2C_MOCK_WRITE, addr, {cmd_hum}, 1, {0}, 0, ESP_OK},
        {I2C_MOCK_READ, addr, {0}, 0, {hum_bytes[0], hum_bytes[1], hum_crc}, 3, ESP_OK},
    };
    i2c_mock_set_sequence(ops, sizeof(ops) / sizeof(ops[0]));

    esp_err_t err = sht20_read_temperature_humidity(addr, &temperature, &humidity);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 23.5f, temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 52.1f, humidity);
    i2c_mock_assert_complete();
}
