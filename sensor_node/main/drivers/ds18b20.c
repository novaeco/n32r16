#include "ds18b20.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "onewire_bus.h"
#include <string.h>

#define DS18B20_CMD_SKIP_ROM 0xCC
#define DS18B20_CMD_MATCH_ROM 0x55
#define DS18B20_CMD_CONVERT_T 0x44
#define DS18B20_CMD_WRITE_SCRATCH 0x4E
#define DS18B20_CMD_READ_SCRATCH 0xBE

static const char *TAG = "ds18b20";

static esp_err_t ds18b20_select_device(onewire_bus_handle_t bus, const onewire_device_t *device)
{
    ESP_RETURN_ON_ERROR(onewire_bus_reset(bus), TAG, "reset");
    if (!device) {
        uint8_t cmd = DS18B20_CMD_SKIP_ROM;
        return onewire_bus_write_bytes(bus, &cmd, 1);
    }
    uint8_t cmd = DS18B20_CMD_MATCH_ROM;
    ESP_RETURN_ON_ERROR(onewire_bus_write_bytes(bus, &cmd, 1), TAG, "match");
    return onewire_bus_write_bytes(bus, device->rom_code, sizeof(device->rom_code));
}

uint32_t ds18b20_conversion_time_ms(uint8_t resolution_bits)
{
    switch (resolution_bits) {
    case 9:
        return 94U;
    case 10:
        return 188U;
    case 11:
        return 375U;
    case 12:
    default:
        return 750U;
    }
}

esp_err_t ds18b20_start_conversion(onewire_bus_handle_t bus, const onewire_device_t *devices,
                                   size_t count, uint8_t resolution_bits)
{
    if (!bus || !devices || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t config = 0x7F;
    switch (resolution_bits) {
    case 9:
        config = 0x1F;
        break;
    case 10:
        config = 0x3F;
        break;
    case 11:
        config = 0x5F;
        break;
    case 12:
    default:
        config = 0x7F;
        break;
    }

    for (size_t i = 0; i < count; ++i) {
        ESP_RETURN_ON_ERROR(ds18b20_select_device(bus, &devices[i]), TAG, "select");
        uint8_t cmd = DS18B20_CMD_WRITE_SCRATCH;
        uint8_t payload[3] = {0x00, 0x00, config};
        ESP_RETURN_ON_ERROR(onewire_bus_write_bytes(bus, &cmd, 1), TAG, "write_cmd");
        ESP_RETURN_ON_ERROR(onewire_bus_write_bytes(bus, payload, sizeof(payload)), TAG, "write");
    }

    ESP_RETURN_ON_ERROR(ds18b20_select_device(bus, NULL), TAG, "skip");
    uint8_t cmd = DS18B20_CMD_CONVERT_T;
    return onewire_bus_write_bytes(bus, &cmd, 1);
}

esp_err_t ds18b20_check_conversion(onewire_bus_handle_t bus, bool *ready)
{
    if (!bus || !ready) {
        return ESP_ERR_INVALID_ARG;
    }
    bool bit = false;
    esp_err_t err = onewire_bus_read_bit(bus, &bit);
    if (err == ESP_OK) {
        *ready = bit;
    }
    return err;
}

esp_err_t ds18b20_read_temperature(onewire_bus_handle_t bus, const onewire_device_t *device,
                                   float *temperature_c)
{
    if (!bus || !device || !temperature_c) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(ds18b20_select_device(bus, device), TAG, "select");
    uint8_t cmd = DS18B20_CMD_READ_SCRATCH;
    ESP_RETURN_ON_ERROR(onewire_bus_write_bytes(bus, &cmd, 1), TAG, "read_cmd");
    uint8_t scratch[9] = {0};
    ESP_RETURN_ON_ERROR(onewire_bus_read_bytes(bus, scratch, sizeof(scratch)), TAG, "read");

    uint16_t raw = ((uint16_t)scratch[1] << 8) | scratch[0];
    *temperature_c = (float)raw / 16.0f;
    return ESP_OK;
}
