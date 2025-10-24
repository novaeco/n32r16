#include "tasks/t_sensors.h"

#include "drivers/ds18b20.h"
#include "drivers/sht20.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io/io_map.h"
#include "onewire_bus_manager.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SENSOR_TASK_PERIOD_MS 200
#define DS18B20_RESOLUTION_BITS 12
#define DS18B20_SCAN_INTERVAL_MS 60000
#define SHT20_EMA_ALPHA 0.25f

typedef struct {
    sensor_data_model_t *model;
    onewire_bus_handle_t bus;
    onewire_device_t ds_devices[4];
    size_t ds_count;
    bool ds_conversion_pending;
    TickType_t ds_ready_tick;
    TickType_t next_ds_scan_tick;
    float sht20_temp_ema[2];
    float sht20_hum_ema[2];
    bool sht20_initialized[2];
} sensors_task_ctx_t;

static const char *TAG = "t_sensors";
static sensors_task_ctx_t s_ctx;

static float ema_update(float prev, float sample, bool *initialized)
{
    if (!initialized) {
        return sample;
    }
    if (!*initialized) {
        *initialized = true;
        return sample;
    }
    return prev + SHT20_EMA_ALPHA * (sample - prev);
}

static void update_sht20_readings(void)
{
    const io_map_t *map = io_map_get();
    for (size_t i = 0; i < 2; ++i) {
        float temp = 0.0f;
        float hum = 0.0f;
        esp_err_t err = sht20_read_temperature_humidity(map->sht20_addresses[i], &temp, &hum);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SHT20 %zu read failed: %s, using synthetic telemetry", i,
                     esp_err_to_name(err));
            TickType_t ticks = xTaskGetTickCount();
            float t_demo = 22.5f + 0.75f * sinf((float)ticks / 750.0f + (float)i);
            float h_demo = 55.0f + 4.0f * cosf((float)ticks / 900.0f + (float)i);
            temp = t_demo;
            hum = h_demo;
        }
        temp = ema_update(s_ctx.sht20_temp_ema[i], temp, &s_ctx.sht20_initialized[i]);
        hum = ema_update(s_ctx.sht20_hum_ema[i], hum, &s_ctx.sht20_initialized[i]);
        s_ctx.sht20_temp_ema[i] = temp;
        s_ctx.sht20_hum_ema[i] = hum;

        char id[16];
        snprintf(id, sizeof(id), "SHT20_%u", (unsigned)i + 1U);
        data_model_set_sht20(s_ctx.model, i, id, temp, hum);
    }
}

static void ensure_ds18b20_devices(void)
{
    TickType_t now = xTaskGetTickCount();
    if (s_ctx.ds_count > 0 && now < s_ctx.next_ds_scan_tick) {
        return;
    }
    size_t count = 0;
    esp_err_t err = onewire_bus_scan(s_ctx.bus, s_ctx.ds_devices, 4, &count);
    if (err == ESP_OK) {
        if (count == 0) {
            ESP_LOGW(TAG, "No DS18B20 sensors discovered on last scan");
        } else if (count != s_ctx.ds_count) {
            ESP_LOGI(TAG, "Discovered %u DS18B20 sensors", (unsigned)count);
        }
        s_ctx.ds_count = count;
    } else {
        ESP_LOGE(TAG, "1-Wire scan failed: %s", esp_err_to_name(err));
    }
    s_ctx.next_ds_scan_tick = now + pdMS_TO_TICKS(DS18B20_SCAN_INTERVAL_MS);
}

static void kick_ds18b20_conversion(void)
{
    if (s_ctx.ds_count == 0 || s_ctx.ds_conversion_pending) {
        return;
    }
    esp_err_t err =
        ds18b20_start_conversion(s_ctx.bus, s_ctx.ds_devices, s_ctx.ds_count, DS18B20_RESOLUTION_BITS);
    if (err == ESP_OK) {
        uint32_t wait_ms = ds18b20_conversion_time_ms(DS18B20_RESOLUTION_BITS);
        s_ctx.ds_ready_tick = xTaskGetTickCount() + pdMS_TO_TICKS(wait_ms);
        s_ctx.ds_conversion_pending = true;
    } else {
        ESP_LOGE(TAG, "Failed to start DS18B20 conversion: %s", esp_err_to_name(err));
    }
}

static bool ds18b20_conversion_complete(void)
{
    if (!s_ctx.ds_conversion_pending) {
        return false;
    }
    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(now - s_ctx.ds_ready_tick) >= 0) {
        return true;
    }
    bool ready = false;
    if (ds18b20_check_conversion(s_ctx.bus, &ready) == ESP_OK) {
        return ready;
    }
    return false;
}

static void read_ds18b20_temperatures(void)
{
    if (!ds18b20_conversion_complete()) {
        return;
    }
    for (size_t i = 0; i < s_ctx.ds_count && i < 4; ++i) {
        float temp = 0.0f;
        esp_err_t err = ds18b20_read_temperature(s_ctx.bus, &s_ctx.ds_devices[i], &temp);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "DS18B20[%zu] read failed: %s, falling back to synthetic data", i,
                     esp_err_to_name(err));
            TickType_t ticks = xTaskGetTickCount();
            temp = 21.0f + 0.5f * sinf((float)ticks / 1200.0f + (float)i);
        }
        data_model_set_ds18b20(s_ctx.model, i, &s_ctx.ds_devices[i], temp);
    }
    s_ctx.ds_conversion_pending = false;
}

static void sensors_task(void *arg)
{
    (void)arg;
    memset(&s_ctx.sht20_temp_ema, 0, sizeof(s_ctx.sht20_temp_ema));
    memset(&s_ctx.sht20_hum_ema, 0, sizeof(s_ctx.sht20_hum_ema));
    memset(&s_ctx.sht20_initialized, 0, sizeof(s_ctx.sht20_initialized));
    s_ctx.ds_count = 0;
    s_ctx.ds_conversion_pending = false;
    s_ctx.next_ds_scan_tick = xTaskGetTickCount();

    ensure_ds18b20_devices();
    kick_ds18b20_conversion();

    while (true) {
        update_sht20_readings();
        ensure_ds18b20_devices();
        read_ds18b20_temperatures();
        kick_ds18b20_conversion();
        vTaskDelay(pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}

void sensors_task_start(sensor_data_model_t *model, onewire_bus_handle_t bus)
{
    s_ctx.model = model;
    s_ctx.bus = bus;
    xTaskCreatePinnedToCore(sensors_task, "t_sensors", 6144, NULL, 5, NULL, 1);
}
