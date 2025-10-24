#include "ds18b20.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DS18B20_CMD_SKIP_ROM 0xCC
#define DS18B20_CMD_MATCH_ROM 0x55
#define DS18B20_CMD_CONVERT_T 0x44
#define DS18B20_CMD_WRITE_SCRATCH 0x4E
#define DS18B20_CMD_READ_SCRATCH 0xBE

static esp_err_t ds18b20_select_device(onewire_bus_handle_t bus, const onewire_device_t *device)
{
    ESP_RETURN_ON_ERROR(onewire_bus_reset(bus), "ds18b20", "reset");
    if (!device) {
        uint8_t cmd = DS18B20_CMD_SKIP_ROM;
        return onewire_bus_write_bytes(bus, &cmd, 1);
    }
    uint8_t cmd = DS18B20_CMD_MATCH_ROM;
    ESP_RETURN_ON_ERROR(onewire_bus_write_bytes(bus, &cmd, 1), "ds18b20", "match");
    return onewire_bus_write_bytes(bus, device->rom_code, sizeof(device->rom_code));
}

esp_err_t ds18b20_start_conversion(onewire_bus_handle_t bus, const onewire_device_t *devices,
                                   size_t count, uint8_t resolution_bits)
{
    if (count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t config;
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
        resolution_bits = 12;
        break;
    }

    for (size_t i = 0; i < count; ++i) {
        ESP_RETURN_ON_ERROR(ds18b20_select_device(bus, &devices[i]), "ds18b20", "select");
        uint8_t scratch[4] = {0x00, 0x00, config, DS18B20_CMD_WRITE_SCRATCH};
        scratch[0] = 0; // TH register
        scratch[1] = 0; // TL register
        scratch[3] = 0; // placeholder
        uint8_t cmd = DS18B20_CMD_WRITE_SCRATCH;
        ESP_RETURN_ON_ERROR(onewire_bus_write_bytes(bus, &cmd, 1), "ds18b20", "write_cmd");
        uint8_t payload[3] = {scratch[0], scratch[1], config};
        ESP_RETURN_ON_ERROR(onewire_bus_write_bytes(bus, payload, sizeof(payload)), "ds18b20", "write");
    }

    ESP_RETURN_ON_ERROR(ds18b20_select_device(bus, NULL), "ds18b20", "skip");
    uint8_t cmd = DS18B20_CMD_CONVERT_T;
    ESP_RETURN_ON_ERROR(onewire_bus_write_bytes(bus, &cmd, 1), "ds18b20", "convert");

    uint32_t delay_ms = 750;
    switch (resolution_bits) {
    case 9:
        delay_ms = 94;
        break;
    case 10:
        delay_ms = 188;
        break;
    case 11:
        delay_ms = 375;
        break;
    default:
        delay_ms = 750;
        break;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    return ESP_OK;
}

esp_err_t ds18b20_read_temperature(onewire_bus_handle_t bus, const onewire_device_t *device,
                                   float *temperature_c)
{
    if (!temperature_c || !device) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(ds18b20_select_device(bus, device), "ds18b20", "select");
    uint8_t cmd = DS18B20_CMD_READ_SCRATCH;
    ESP_RETURN_ON_ERROR(onewire_bus_write_bytes(bus, &cmd, 1), "ds18b20", "read_cmd");
    uint8_t scratch[9] = {0};
    ESP_RETURN_ON_ERROR(onewire_bus_read_bytes(bus, scratch, sizeof(scratch)), "ds18b20", "read");

    uint16_t raw = ((uint16_t)scratch[1] << 8) | scratch[0];
    *temperature_c = (float)raw / 16.0f;
    return ESP_OK;
}
