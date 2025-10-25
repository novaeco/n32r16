#include "drivers/bme280.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include <string.h>

#define TAG "bme280"

#define BME280_MAX_DEVICES 4

#define BME280_REG_CALIB_TPH 0x88
#define BME280_REG_CALIB_H1 0xA1
#define BME280_REG_CALIB_H2 0xE1
#define BME280_REG_CTRL_HUM 0xF2
#define BME280_REG_STATUS 0xF3
#define BME280_REG_CTRL_MEAS 0xF4
#define BME280_REG_CONFIG 0xF5
#define BME280_REG_DATA 0xF7
#define BME280_REG_RESET 0xE0

#define BME280_RESET_CMD 0xB6

#define BME280_OSRS_X1 0x01
#define BME280_MODE_FORCED 0x01
#define BME280_FILTER_COEFF_4 0x02
#define BME280_STANDBY_62_5_MS 0x03

#define BME280_STATUS_MEASURING 0x08

#define BME280_MEASUREMENT_TIMEOUT_MS 40

typedef struct {
    bool in_use;
    bool calibrated;
    uint8_t address;
    struct {
        uint16_t dig_T1;
        int16_t dig_T2;
        int16_t dig_T3;
        uint16_t dig_P1;
        int16_t dig_P2;
        int16_t dig_P3;
        int16_t dig_P4;
        int16_t dig_P5;
        int16_t dig_P6;
        int16_t dig_P7;
        int16_t dig_P8;
        int16_t dig_P9;
        uint8_t dig_H1;
        int16_t dig_H2;
        uint8_t dig_H3;
        int16_t dig_H4;
        int16_t dig_H5;
        int8_t dig_H6;
    } calib;
} bme280_device_t;

static bme280_device_t s_devices[BME280_MAX_DEVICES];

static esp_err_t bme280_write_reg(uint8_t address, uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    return i2c_bus_write(address, payload, sizeof(payload));
}

static esp_err_t bme280_read_reg(uint8_t address, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_bus_write_read(address, &reg, 1, buf, len);
}

static bme280_device_t *bme280_get_device(uint8_t address)
{
    for (size_t i = 0; i < BME280_MAX_DEVICES; ++i) {
        if (s_devices[i].in_use && s_devices[i].address == address) {
            return &s_devices[i];
        }
    }
    for (size_t i = 0; i < BME280_MAX_DEVICES; ++i) {
        if (!s_devices[i].in_use) {
            s_devices[i].in_use = true;
            s_devices[i].address = address;
            s_devices[i].calibrated = false;
            memset(&s_devices[i].calib, 0, sizeof(s_devices[i].calib));
            return &s_devices[i];
        }
    }
    return NULL;
}

static esp_err_t bme280_reset(uint8_t address)
{
    return bme280_write_reg(address, BME280_REG_RESET, BME280_RESET_CMD);
}

static esp_err_t bme280_wait_measuring(uint8_t address)
{
    uint32_t elapsed = 0;
    uint8_t status = 0;
    do {
        ESP_RETURN_ON_ERROR(bme280_read_reg(address, BME280_REG_STATUS, &status, 1), TAG, "read status");
        if ((status & BME280_STATUS_MEASURING) == 0) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(4));
        elapsed += 4;
    } while (elapsed < BME280_MEASUREMENT_TIMEOUT_MS);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t bme280_load_calibration(bme280_device_t *dev)
{
    if (!dev || dev->calibrated) {
        return ESP_OK;
    }
    uint8_t calib_tph[26] = {0};
    ESP_RETURN_ON_ERROR(bme280_read_reg(dev->address, BME280_REG_CALIB_TPH, calib_tph, sizeof(calib_tph)), TAG,
                        "read calib TPH");
    dev->calib.dig_T1 = (uint16_t)(calib_tph[0] | (calib_tph[1] << 8));
    dev->calib.dig_T2 = (int16_t)(calib_tph[2] | (calib_tph[3] << 8));
    dev->calib.dig_T3 = (int16_t)(calib_tph[4] | (calib_tph[5] << 8));
    dev->calib.dig_P1 = (uint16_t)(calib_tph[6] | (calib_tph[7] << 8));
    dev->calib.dig_P2 = (int16_t)(calib_tph[8] | (calib_tph[9] << 8));
    dev->calib.dig_P3 = (int16_t)(calib_tph[10] | (calib_tph[11] << 8));
    dev->calib.dig_P4 = (int16_t)(calib_tph[12] | (calib_tph[13] << 8));
    dev->calib.dig_P5 = (int16_t)(calib_tph[14] | (calib_tph[15] << 8));
    dev->calib.dig_P6 = (int16_t)(calib_tph[16] | (calib_tph[17] << 8));
    dev->calib.dig_P7 = (int16_t)(calib_tph[18] | (calib_tph[19] << 8));
    dev->calib.dig_P8 = (int16_t)(calib_tph[20] | (calib_tph[21] << 8));
    dev->calib.dig_P9 = (int16_t)(calib_tph[22] | (calib_tph[23] << 8));
    dev->calib.dig_H1 = calib_tph[25];

    uint8_t calib_h1 = 0;
    ESP_RETURN_ON_ERROR(bme280_read_reg(dev->address, BME280_REG_CALIB_H1, &calib_h1, 1), TAG, "read calib H1");
    dev->calib.dig_H1 = calib_h1;

    uint8_t calib_h[7] = {0};
    ESP_RETURN_ON_ERROR(bme280_read_reg(dev->address, BME280_REG_CALIB_H2, calib_h, sizeof(calib_h)), TAG,
                        "read calib H");
    dev->calib.dig_H2 = (int16_t)(calib_h[0] | (calib_h[1] << 8));
    dev->calib.dig_H3 = calib_h[2];
    dev->calib.dig_H4 = (int16_t)((calib_h[3] << 4) | (calib_h[4] & 0x0F));
    dev->calib.dig_H5 = (int16_t)((calib_h[5] << 4) | (calib_h[4] >> 4));
    dev->calib.dig_H6 = (int8_t)calib_h[6];

    dev->calibrated = true;
    return ESP_OK;
}

static esp_err_t bme280_configure(bme280_device_t *dev)
{
    ESP_RETURN_ON_ERROR(bme280_write_reg(dev->address, BME280_REG_CTRL_HUM, BME280_OSRS_X1), TAG, "ctrl hum");
    uint8_t config = (uint8_t)((BME280_STANDBY_62_5_MS << 5) | (BME280_FILTER_COEFF_4 << 2));
    ESP_RETURN_ON_ERROR(bme280_write_reg(dev->address, BME280_REG_CONFIG, config), TAG, "config");
    return ESP_OK;
}

static float bme280_compensate_temperature(const bme280_device_t *dev, int32_t adc_T, int32_t *t_fine)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dev->calib.dig_T1 << 1))) * ((int32_t)dev->calib.dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)dev->calib.dig_T1)) * ((adc_T >> 4) - ((int32_t)dev->calib.dig_T1))) >> 12) *
                    ((int32_t)dev->calib.dig_T3)) >> 14;
    *t_fine = var1 + var2;
    int32_t temp = (*t_fine * 5 + 128) >> 8;
    return temp / 100.0f;
}

static float bme280_compensate_pressure(const bme280_device_t *dev, int32_t adc_P, int32_t t_fine)
{
    int64_t var1 = (int64_t)t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)dev->calib.dig_P6;
    var2 += (var1 * (int64_t)dev->calib.dig_P5) << 17;
    var2 += ((int64_t)dev->calib.dig_P4) << 35;
    var1 = ((var1 * var1 * (int64_t)dev->calib.dig_P3) >> 8) + ((var1 * (int64_t)dev->calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * (int64_t)dev->calib.dig_P1 >> 33;
    if (var1 == 0) {
        return 0.0f;
    }
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)dev->calib.dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)dev->calib.dig_P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)dev->calib.dig_P7) << 4);
    return (float)p / 256.0f;
}

static float bme280_compensate_humidity(const bme280_device_t *dev, int32_t adc_H, int32_t t_fine)
{
    int32_t v_x1_u32r = t_fine - 76800;
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)dev->calib.dig_H4) << 20) - (((int32_t)dev->calib.dig_H5) * v_x1_u32r)) + 16384) >> 15) *
                 (((((((v_x1_u32r * (int32_t)dev->calib.dig_H6) >> 10) * (((v_x1_u32r * (int32_t)dev->calib.dig_H3) >> 11) + 32768)) >> 10) +
                    2097152) * (int32_t)dev->calib.dig_H2 + 8192) >> 14));
    v_x1_u32r = v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * (int32_t)dev->calib.dig_H1) >> 4);
    v_x1_u32r = v_x1_u32r < 0 ? 0 : v_x1_u32r;
    v_x1_u32r = v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r;
    return (float)(v_x1_u32r >> 12) / 1024.0f;
}

static esp_err_t bme280_perform_measurement(bme280_device_t *dev, float *temp, float *humidity, float *pressure)
{
    ESP_RETURN_ON_ERROR(bme280_write_reg(dev->address, BME280_REG_CTRL_HUM, BME280_OSRS_X1), TAG, "ctrl hum");
    uint8_t ctrl_meas = (uint8_t)((BME280_OSRS_X1 << 5) | (BME280_OSRS_X1 << 2) | BME280_MODE_FORCED);
    ESP_RETURN_ON_ERROR(bme280_write_reg(dev->address, BME280_REG_CTRL_MEAS, ctrl_meas), TAG, "ctrl meas");
    ESP_RETURN_ON_ERROR(bme280_wait_measuring(dev->address), TAG, "wait measure");

    uint8_t raw[8] = {0};
    ESP_RETURN_ON_ERROR(bme280_read_reg(dev->address, BME280_REG_DATA, raw, sizeof(raw)), TAG, "read data");

    int32_t adc_P = (int32_t)(((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | ((uint32_t)raw[2] >> 4));
    int32_t adc_T = (int32_t)(((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | ((uint32_t)raw[5] >> 4));
    int32_t adc_H = (int32_t)(((uint32_t)raw[6] << 8) | (uint32_t)raw[7]);

    int32_t t_fine = 0;
    float temperature = bme280_compensate_temperature(dev, adc_T, &t_fine);
    float humidity_local = bme280_compensate_humidity(dev, adc_H, t_fine);
    float pressure_local = bme280_compensate_pressure(dev, adc_P, t_fine);

    if (temp) {
        *temp = temperature;
    }
    if (humidity) {
        *humidity = humidity_local;
    }
    if (pressure) {
        *pressure = pressure_local;
    }
    return ESP_OK;
}

esp_err_t bme280_init(uint8_t address)
{
    bme280_device_t *dev = bme280_get_device(address);
    if (!dev) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(bme280_reset(address), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_RETURN_ON_ERROR(bme280_load_calibration(dev), TAG, "load calib");
    return bme280_configure(dev);
}

esp_err_t bme280_read(uint8_t address, float *temperature_c, float *humidity_percent, float *pressure_pa)
{
    bme280_device_t *dev = bme280_get_device(address);
    if (!dev) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(bme280_load_calibration(dev), TAG, "load calib");
    ESP_RETURN_ON_ERROR(bme280_configure(dev), TAG, "configure");
    return bme280_perform_measurement(dev, temperature_c, humidity_percent, pressure_pa);
}
