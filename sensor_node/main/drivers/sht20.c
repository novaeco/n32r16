#include "sht20.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "time_sync.h"

#define SHT20_CMD_TRIG_T 0xF3
#define SHT20_CMD_TRIG_RH 0xF5

static const char *TAG = "sht20";

static bool sht20_check_crc(uint16_t data, uint8_t crc) {
    uint32_t remainder = ((uint32_t)data << 8) | crc;
    uint32_t divisor = 0x988000;
    for (int i = 0; i < 16; ++i) {
        if (remainder & (1u << (23 - i))) {
            remainder ^= divisor;
        }
        divisor >>= 1;
    }
    return (remainder & 0xFF) == 0;
}

static esp_err_t sht20_read_raw(uint8_t addr, uint8_t command, uint16_t *out_value) {
    esp_err_t err = i2c_bus_write_read(I2C_NUM_0, addr, &command, 1, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Command write failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(85));
    uint8_t buf[3] = {0};
    err = i2c_bus_write_read(I2C_NUM_0, addr, NULL, 0, buf, sizeof(buf));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Data read failed: %s", esp_err_to_name(err));
        return err;
    }
    uint16_t raw = ((uint16_t)buf[0] << 8) | buf[1];
    if (!sht20_check_crc(raw, buf[2])) {
        return ESP_ERR_INVALID_CRC;
    }
    *out_value = raw & ~0x0003;
    return ESP_OK;
}

esp_err_t sht20_init(uint8_t address) {
    (void)address;
    return ESP_OK;
}

esp_err_t sht20_read(uint8_t address, sht20_sample_t *out_sample) {
    if (out_sample == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw_temp = 0;
    uint16_t raw_rh = 0;

    esp_err_t err = sht20_read_raw(address, SHT20_CMD_TRIG_T, &raw_temp);
    if (err != ESP_OK) {
        out_sample->valid = false;
        return err;
    }
    err = sht20_read_raw(address, SHT20_CMD_TRIG_RH, &raw_rh);
    if (err != ESP_OK) {
        out_sample->valid = false;
        return err;
    }

    out_sample->temperature_c = -46.85f + 175.72f * (float)raw_temp / 65536.0f;
    out_sample->humidity_rh = -6.0f + 125.0f * (float)raw_rh / 65536.0f;
    out_sample->timestamp_ms = (uint32_t)time_sync_get_monotonic_ms();
    out_sample->valid = true;
    return ESP_OK;
}

