#include "pca9685.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include <math.h>

#define PCA9685_MODE1 0x00
#define PCA9685_MODE2 0x01
#define PCA9685_LED0_ON_L 0x06
#define PCA9685_ALL_LED_ON_L 0xFA
#define PCA9685_PRESCALE 0xFE

/**
 * @brief Write a single PCA9685 register.
 *
 * @param addr I2C address of the device.
 * @param reg Register address to update.
 * @param value Byte to write into the register.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t pca9685_write8(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_bus_write(addr, data, sizeof(data));
}

/**
 * @brief Write the PCA9685 channel timing registers.
 *
 * @param addr I2C address of the device.
 * @param reg Base register for the channel.
 * @param on Tick value where the PWM signal turns on.
 * @param off Tick value where the PWM signal turns off.
 * @return ESP_OK when the transfer succeeds, otherwise an error code.
 */
static esp_err_t pca9685_write16(uint8_t addr, uint8_t reg, uint16_t on, uint16_t off)
{
    uint8_t data[5] = {
        reg,
        (uint8_t)(on & 0xFF),
        (uint8_t)((on >> 8) & 0x0F),
        (uint8_t)(off & 0xFF),
        (uint8_t)((off >> 8) & 0x0F),
    };
    return i2c_bus_write(addr, data, sizeof(data));
}

/**
 * @brief Initialize the PCA9685 PWM controller.
 *
 * @param addr I2C address of the device.
 * @param frequency_hz Desired PWM frequency in hertz.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
esp_err_t pca9685_init(uint8_t addr, uint16_t frequency_hz)
{
    float prescale = (25000000.0f / (4096.0f * (float)frequency_hz)) - 1.0f;
    uint8_t prescale_val = (uint8_t)(prescale + 0.5f);

    ESP_RETURN_ON_ERROR(pca9685_write8(addr, PCA9685_MODE1, 0x10), "pca9685", "sleep");
    ESP_RETURN_ON_ERROR(pca9685_write8(addr, PCA9685_PRESCALE, prescale_val), "pca9685", "prescale");
    ESP_RETURN_ON_ERROR(pca9685_write8(addr, PCA9685_MODE1, 0xA1), "pca9685", "mode1");
    ESP_RETURN_ON_ERROR(pca9685_write8(addr, PCA9685_MODE2, 0x04), "pca9685", "mode2");
    vTaskDelay(pdMS_TO_TICKS(5));
    return ESP_OK;
}

/**
 * @brief Set the duty cycle for a single PCA9685 channel.
 *
 * @param addr I2C address of the device.
 * @param channel Channel index (0-15).
 * @param duty 12-bit duty cycle value.
 * @return ESP_OK when the channel update succeeds, otherwise an error code.
 */
esp_err_t pca9685_set_pwm(uint8_t addr, uint8_t channel, uint16_t duty)
{
    if (channel >= 16) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t on = 0;
    uint16_t off = duty;
    if (duty >= 4096) {
        on = 0;
        off = 0x1000;
    }
    uint8_t reg = PCA9685_LED0_ON_L + 4 * channel;
    return pca9685_write16(addr, reg, on, off);
}

/**
 * @brief Apply duty cycle values to a contiguous set of channels.
 *
 * @param addr I2C address of the device.
 * @param duty Array of duty cycle values.
 * @param count Number of entries in the duty array.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
esp_err_t pca9685_set_all(uint8_t addr, const uint16_t *duty, size_t count)
{
    if (!duty || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < count; ++i) {
        ESP_RETURN_ON_ERROR(pca9685_set_pwm(addr, i, duty[i]), "pca9685", "set");
    }
    return ESP_OK;
}
