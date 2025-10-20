#include "ds18b20.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "onewire_bus.h"
#include "time_sync.h"

static const char *TAG = "ds18b20";
static size_t s_sensor_count;
static uint64_t s_roms[DS18B20_MAX_SENSORS];

static uint8_t ds18b20_crc(const uint8_t *data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; ++i) {
        uint8_t inbyte = data[i];
        for (uint8_t j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            inbyte >>= 1;
        }
    }
    return crc;
}

esp_err_t ds18b20_scan(ds18b20_sample_t *sensors, size_t *count) {
    if (sensors == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    onewire_device_list_t list;
    if (!onewire_bus_search(&list)) {
        return ESP_FAIL;
    }

    s_sensor_count = list.count;
    if (s_sensor_count > DS18B20_MAX_SENSORS) {
        s_sensor_count = DS18B20_MAX_SENSORS;
    }

    for (size_t i = 0; i < s_sensor_count; ++i) {
        s_roms[i] = list.roms[i];
        sensors[i].rom_code = list.roms[i];
        sensors[i].temperature_c = 0.0f;
        sensors[i].valid = false;
    }
    *count = s_sensor_count;

    return ESP_OK;
}

esp_err_t ds18b20_trigger_conversion(void) {
    if (s_sensor_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!onewire_bus_reset()) {
        return ESP_FAIL;
    }
    uint8_t cmd = 0xCC;  // Skip ROM
    onewire_bus_write(&cmd, 1);
    cmd = 0x44;
    onewire_bus_write(&cmd, 1);
    vTaskDelay(pdMS_TO_TICKS(750));
    return ESP_OK;
}

static void ds18b20_match_rom(uint64_t rom) {
    uint8_t cmd = 0x55;
    onewire_bus_write(&cmd, 1);
    uint8_t rom_bytes[8];
    for (int i = 0; i < 8; ++i) {
        rom_bytes[i] = (uint8_t)(rom >> (8 * i));
    }
    onewire_bus_write(rom_bytes, sizeof(rom_bytes));
}

esp_err_t ds18b20_read_scratchpad(ds18b20_sample_t *sensors, size_t count) {
    if (sensors == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t to_read = count < s_sensor_count ? count : s_sensor_count;
    for (size_t i = 0; i < to_read; ++i) {
        if (!onewire_bus_reset()) {
            sensors[i].valid = false;
            continue;
        }
        ds18b20_match_rom(s_roms[i]);
        uint8_t cmd = 0xBE;
        onewire_bus_write(&cmd, 1);
        uint8_t scratchpad[9] = {0};
        onewire_bus_read(scratchpad, sizeof(scratchpad));
        uint8_t crc = ds18b20_crc(scratchpad, 8);
        if (crc != scratchpad[8]) {
            sensors[i].valid = false;
            ESP_LOGW(TAG, "CRC mismatch for sensor %zu", i);
            continue;
        }
        int16_t raw_temp = (int16_t)((scratchpad[1] << 8) | scratchpad[0]);
        sensors[i].temperature_c = (float)raw_temp / 16.0f;
        sensors[i].rom_code = s_roms[i];
        sensors[i].valid = true;
    }
    return ESP_OK;
}

