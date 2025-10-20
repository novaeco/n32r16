#include "pca9685.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

#define PCA9685_MODE1 0x00
#define PCA9685_MODE2 0x01
#define PCA9685_PRESCALE 0xFE
#define PCA9685_LED0_ON_L 0x06

static const char *TAG = "pca9685";
static pca9685_state_t s_state;

static esp_err_t pca9685_write(uint8_t address, uint8_t reg, const uint8_t *data, size_t len) {
    uint8_t buffer[5];
    buffer[0] = reg;
    memcpy(&buffer[1], data, len);
    return i2c_bus_write_read(I2C_NUM_0, address, buffer, len + 1, NULL, 0);
}

esp_err_t pca9685_init(uint8_t address, uint16_t frequency_hz) {
    memset(&s_state, 0, sizeof(s_state));
    s_state.frequency_hz = frequency_hz;

    uint8_t mode1 = 0x00;
    uint8_t mode2 = 0x04;
    ESP_ERROR_CHECK(pca9685_write(address, PCA9685_MODE1, &mode1, 1));
    ESP_ERROR_CHECK(pca9685_write(address, PCA9685_MODE2, &mode2, 1));

    float prescaleval = 25000000.0f;
    prescaleval /= 4096.0f;
    prescaleval /= (float)frequency_hz;
    prescaleval -= 1.0f;
    if (prescaleval < 3.0f) {
        prescaleval = 3.0f;
    }
    uint8_t prescale = (uint8_t)prescaleval;

    uint8_t sleep = 0x10;
    ESP_ERROR_CHECK(pca9685_write(address, PCA9685_MODE1, &sleep, 1));
    ESP_ERROR_CHECK(pca9685_write(address, PCA9685_PRESCALE, &prescale, 1));
    vTaskDelay(pdMS_TO_TICKS(5));
    mode1 = 0x20;
    ESP_ERROR_CHECK(pca9685_write(address, PCA9685_MODE1, &mode1, 1));
    vTaskDelay(pdMS_TO_TICKS(5));
    return ESP_OK;
}

esp_err_t pca9685_set_pwm(uint8_t address, uint8_t channel, uint16_t duty) {
    if (channel >= 16 || duty > 4095) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t on_count = 0;
    uint16_t off_count = duty;
    uint8_t reg = PCA9685_LED0_ON_L + 4 * channel;
    uint8_t buffer[4] = {
        (uint8_t)(on_count & 0xFF),
        (uint8_t)(on_count >> 8),
        (uint8_t)(off_count & 0xFF),
        (uint8_t)(off_count >> 8),
    };
    esp_err_t err = pca9685_write(address, reg, buffer, sizeof(buffer));
    if (err == ESP_OK) {
        s_state.duty_cycle[channel] = duty;
    }
    return err;
}

esp_err_t pca9685_snapshot(uint8_t address, pca9685_state_t *state) {
    (void)address;
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *state = s_state;
    return ESP_OK;
}

