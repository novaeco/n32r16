#include "sht20.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include <math.h>
#include <stddef.h>

#define SHT20_CMD_TRIGGER_TEMP 0xF3
#define SHT20_CMD_TRIGGER_HUM 0xF5
#define SHT20_CMD_SOFT_RESET 0xFE

#define SHT20_MAX_RETRIES 5
#define SHT20_BASE_BACKOFF_MS 10

static const char *TAG = "sht20";

/**
 * @brief Compute the SHT20 CRC-8 checksum for a measurement frame.
 *
 * @param data Pointer to the byte buffer to checksum.
 * @param len Number of bytes to process.
 * @return Calculated CRC value.
 */
static uint8_t sht20_crc8(const uint8_t *data, size_t len)
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

/**
 * @brief Trigger a single SHT20 measurement and read the raw response bytes.
 *
 * @param addr I2C address of the sensor.
 * @param command Measurement command opcode.
 * @param raw Output buffer receiving three bytes (MSB, LSB, CRC).
 * @return ESP_OK on success or an I2C driver error code.
 */
static esp_err_t sht20_measure_once(uint8_t addr, uint8_t command, uint8_t *raw)
{
    ESP_RETURN_ON_ERROR(i2c_bus_write(addr, &command, 1), TAG, "command");
    const TickType_t wait_time = pdMS_TO_TICKS(command == SHT20_CMD_TRIGGER_TEMP ? 85 : 29);
    vTaskDelay(wait_time);
    return i2c_bus_read(addr, raw, 3);
}

/**
 * @brief Convert an SHT20 measurement into engineering units with retries.
 *
 * @param addr I2C address of the sensor.
 * @param command Measurement command opcode.
 * @param out_value Output pointer receiving the temperature or humidity value.
 * @param is_temp True when the measurement represents temperature, false for humidity.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t sht20_read_measurement(uint8_t addr, uint8_t command, float *out_value, bool is_temp)
{
    uint8_t buffer[3] = {0};
    esp_err_t last_err = ESP_FAIL;
    for (int attempt = 0; attempt < SHT20_MAX_RETRIES; ++attempt) {
        last_err = sht20_measure_once(addr, command, buffer);
        if (last_err != ESP_OK) {
            ESP_LOGW(TAG, "I2C error reading 0x%02X (attempt %d/%d): %s", command, attempt + 1,
                     SHT20_MAX_RETRIES, esp_err_to_name(last_err));
        } else if (sht20_crc8(buffer, 2) != buffer[2]) {
            last_err = ESP_ERR_INVALID_CRC;
            ESP_LOGW(TAG, "CRC mismatch from sensor 0x%02X (attempt %d/%d)", addr, attempt + 1,
                     SHT20_MAX_RETRIES);
        } else {
            uint16_t raw = ((uint16_t)buffer[0] << 8) | buffer[1];
            raw &= ~0x0003U;
            if (is_temp) {
                *out_value = -46.85f + 175.72f * (float)raw / 65536.0f;
            } else {
                float humidity = -6.0f + 125.0f * (float)raw / 65536.0f;
                *out_value = fminf(fmaxf(humidity, 0.0f), 100.0f);
            }
            return ESP_OK;
        }
        if (attempt >= 2) {
            ESP_LOGI(TAG, "Soft resetting SHT20 at 0x%02X after repeated failures", addr);
            uint8_t cmd = SHT20_CMD_SOFT_RESET;
            if (i2c_bus_write(addr, &cmd, 1) == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        uint32_t backoff = SHT20_BASE_BACKOFF_MS << attempt;
        vTaskDelay(pdMS_TO_TICKS(backoff));
    }
    return last_err;
}

/**
 * @brief Send the soft-reset command to the SHT20 sensor.
 *
 * @param addr I2C address of the sensor.
 * @return ESP_OK on success or an I2C error code.
 */
esp_err_t sht20_soft_reset(uint8_t addr)
{
    uint8_t cmd = SHT20_CMD_SOFT_RESET;
    ESP_RETURN_ON_ERROR(i2c_bus_write(addr, &cmd, 1), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(15));
    return ESP_OK;
}

/**
 * @brief Read both temperature and humidity from the SHT20 sensor.
 *
 * @param addr I2C address of the sensor.
 * @param temperature_c Output pointer receiving the temperature in Celsius.
 * @param humidity Output pointer receiving the relative humidity percentage.
 * @return ESP_OK when both measurements succeed, otherwise an error code.
 */
esp_err_t sht20_read_temperature_humidity(uint8_t addr, float *temperature_c, float *humidity)
{
    ESP_RETURN_ON_ERROR(
        sht20_read_measurement(addr, SHT20_CMD_TRIGGER_TEMP, temperature_c, true), TAG, "temp");
    return sht20_read_measurement(addr, SHT20_CMD_TRIGGER_HUM, humidity, false);
}
