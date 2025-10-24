#include "sht20.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include <stddef.h>

#define SHT20_CMD_TRIGGER_TEMP 0xF3
#define SHT20_CMD_TRIGGER_HUM 0xF5
#define SHT20_CMD_SOFT_RESET 0xFE

static uint8_t sht20_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x131;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static esp_err_t sht20_read_measurement(uint8_t addr, uint8_t command, float *out_value, bool is_temp)
{
    ESP_RETURN_ON_ERROR(i2c_bus_write(addr, &command, 1), "sht20", "command");
    vTaskDelay(pdMS_TO_TICKS(is_temp ? 85 : 29));

    uint8_t buffer[3] = {0};
    ESP_RETURN_ON_ERROR(i2c_bus_read(addr, buffer, sizeof(buffer)), "sht20", "read");

    if (sht20_crc8(buffer, 2) != buffer[2]) {
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw = ((uint16_t)buffer[0] << 8) | buffer[1];
    raw &= ~0x0003; // clear status bits
    if (is_temp) {
        *out_value = -46.85f + 175.72f * (float)raw / 65536.0f;
    } else {
        *out_value = -6.0f + 125.0f * (float)raw / 65536.0f;
    }
    return ESP_OK;
}

esp_err_t sht20_soft_reset(uint8_t addr)
{
    uint8_t cmd = SHT20_CMD_SOFT_RESET;
    ESP_RETURN_ON_ERROR(i2c_bus_write(addr, &cmd, 1), "sht20", "reset");
    vTaskDelay(pdMS_TO_TICKS(15));
    return ESP_OK;
}

esp_err_t sht20_read_temperature_humidity(uint8_t addr, float *temperature_c, float *humidity)
{
    ESP_RETURN_ON_ERROR(sht20_read_measurement(addr, SHT20_CMD_TRIGGER_TEMP, temperature_c, true),
                        "sht20", "temp");
    ESP_RETURN_ON_ERROR(sht20_read_measurement(addr, SHT20_CMD_TRIGGER_HUM, humidity, false),
                        "sht20", "hum");
    return ESP_OK;
}
