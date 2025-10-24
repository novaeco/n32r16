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

static esp_err_t pca9685_write8(uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_bus_write(addr, data, sizeof(data));
}

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
