#include "tasks/t_sensors.h"

#include "drivers/ds18b20.h"
#if CONFIG_SENSOR_AMBIENT_SENSOR_SHT20
#include "drivers/sht20.h"
#include "drivers/tca9548a.h"
#endif
#if CONFIG_SENSOR_AMBIENT_SENSOR_BME280
#include "drivers/bme280.h"
#endif
#include "esp_check.h"
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
#define AMBIENT_EMA_ALPHA 0.25f

typedef struct {
    sensor_data_model_t *model;
    onewire_bus_handle_t bus;
    onewire_device_t ds_devices[4];
    size_t ds_count;
    bool ds_conversion_pending;
    TickType_t ds_ready_tick;
    TickType_t next_ds_scan_tick;
    size_t ambient_count;
    float ambient_temp_ema[IO_MAX_AMBIENT_SENSORS];
    float ambient_hum_ema[IO_MAX_AMBIENT_SENSORS];
    bool ambient_initialized[IO_MAX_AMBIENT_SENSORS];
} sensors_task_ctx_t;

static const char *TAG = "t_sensors";
static sensors_task_ctx_t s_ctx;

/**
 * @brief Update an exponential moving average sample.
 *
 * @param prev Previous filtered value.
 * @param sample New raw sample to incorporate.
 * @param initialized Pointer to the EMA initialization flag for the channel.
 * @return Updated EMA output.
 */
static float ema_update(float prev, float sample, bool *initialized)
{
    if (!initialized) {
        return sample;
    }
    if (!*initialized) {
        *initialized = true;
        return sample;
    }
    return prev + AMBIENT_EMA_ALPHA * (sample - prev);
}

static esp_err_t select_ambient_channel(const io_ambient_sensor_t *sensor)
{
    if (!sensor) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sensor->mux_channel == IO_MUX_CHANNEL_NONE || sensor->mux_address == 0) {
        return ESP_OK;
    }
#if CONFIG_SENSOR_AMBIENT_SENSOR_SHT20 || CONFIG_SENSOR_AMBIENT_SENSOR_BME280
    return tca9548a_select(sensor->mux_address, sensor->mux_channel);
#else
    return ESP_OK;
#endif
}

static const char *ambient_sensor_name(io_ambient_sensor_type_t type)
{
    switch (type) {
    case IO_AMBIENT_SENSOR_SHT20:
        return "SHT20";
    case IO_AMBIENT_SENSOR_BME280:
        return "BME280";
    default:
        return "AMBIENT";
    }
}

static esp_err_t ambient_sensor_init(const io_ambient_sensor_t *sensor)
{
    if (!sensor) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (sensor->type) {
    case IO_AMBIENT_SENSOR_SHT20:
#if CONFIG_SENSOR_AMBIENT_SENSOR_SHT20
        return select_ambient_channel(sensor);
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    case IO_AMBIENT_SENSOR_BME280:
#if CONFIG_SENSOR_AMBIENT_SENSOR_BME280
        ESP_RETURN_ON_ERROR(select_ambient_channel(sensor), TAG, "mux select");
        return bme280_init(sensor->address);
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

static esp_err_t ambient_sensor_read(const io_ambient_sensor_t *sensor, float *temp, float *humidity)
{
    if (!sensor || !temp || !humidity) {
        return ESP_ERR_INVALID_ARG;
    }
    switch (sensor->type) {
    case IO_AMBIENT_SENSOR_SHT20:
#if CONFIG_SENSOR_AMBIENT_SENSOR_SHT20
        ESP_RETURN_ON_ERROR(select_ambient_channel(sensor), TAG, "mux select");
        return sht20_read_temperature_humidity(sensor->address, temp, humidity);
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    case IO_AMBIENT_SENSOR_BME280:
#if CONFIG_SENSOR_AMBIENT_SENSOR_BME280
        ESP_RETURN_ON_ERROR(select_ambient_channel(sensor), TAG, "mux select");
        return bme280_read(sensor->address, temp, humidity, NULL);
#else
        return ESP_ERR_NOT_SUPPORTED;
#endif
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

/**
 * @brief Refresh cached ambient measurements and publish them to the data model.
 *
 * @return void
 */
static void update_ambient_readings(void)
{
    const io_map_t *map = io_map_get();
    for (size_t i = 0; i < s_ctx.ambient_count && i < IO_MAX_AMBIENT_SENSORS; ++i) {
        const io_ambient_sensor_t *sensor = &map->ambient[i];
        float temp = 0.0f;
        float hum = 0.0f;
        esp_err_t err = ambient_sensor_read(sensor, &temp, &hum);
        bool valid = (err == ESP_OK);
        if (!valid) {
            ESP_LOGW(TAG, "%s %zu read failed: %s", ambient_sensor_name(sensor->type), i, esp_err_to_name(err));
        }
        float filtered_temp = s_ctx.ambient_temp_ema[i];
        float filtered_hum = s_ctx.ambient_hum_ema[i];
        if (valid) {
            filtered_temp = ema_update(filtered_temp, temp, &s_ctx.ambient_initialized[i]);
            filtered_hum = ema_update(filtered_hum, hum, &s_ctx.ambient_initialized[i]);
            s_ctx.ambient_temp_ema[i] = filtered_temp;
            s_ctx.ambient_hum_ema[i] = filtered_hum;
        } else if (!s_ctx.ambient_initialized[i]) {
            filtered_temp = NAN;
            filtered_hum = NAN;
        }

        char id[16];
        snprintf(id, sizeof(id), "%s_%u", ambient_sensor_name(sensor->type), (unsigned)i + 1U);
        data_model_set_sht20(s_ctx.model, i, id, filtered_temp, filtered_hum, valid);
    }
}

/**
 * @brief Scan the 1-Wire bus to maintain the list of DS18B20 sensors.
 *
 * @return void
 */
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

/**
 * @brief Start a new conversion on the discovered DS18B20 devices.
 *
 * @return void
 */
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

/**
 * @brief Check whether the pending DS18B20 conversion finished.
 *
 * @return true if the conversion is complete, false otherwise.
 */
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

/**
 * @brief Read temperatures from DS18B20 sensors and update the data model.
 *
 * @return void
 */
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

/**
 * @brief FreeRTOS task body responsible for periodic sensor acquisition.
 *
 * @param arg Unused task argument.
 * @return void
 */
static void sensors_task(void *arg)
{
    (void)arg;
    memset(&s_ctx.ambient_temp_ema, 0, sizeof(s_ctx.ambient_temp_ema));
    memset(&s_ctx.ambient_hum_ema, 0, sizeof(s_ctx.ambient_hum_ema));
    memset(&s_ctx.ambient_initialized, 0, sizeof(s_ctx.ambient_initialized));
    const io_map_t *map = io_map_get();
    s_ctx.ambient_count = map->ambient_count < IO_MAX_AMBIENT_SENSORS ? map->ambient_count : IO_MAX_AMBIENT_SENSORS;
    for (size_t i = 0; i < s_ctx.ambient_count; ++i) {
        esp_err_t err = ambient_sensor_init(&map->ambient[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "%s %zu init failed: %s", ambient_sensor_name(map->ambient[i].type), i,
                     esp_err_to_name(err));
        }
    }
    s_ctx.ds_count = 0;
    s_ctx.ds_conversion_pending = false;
    s_ctx.next_ds_scan_tick = xTaskGetTickCount();

    ensure_ds18b20_devices();
    kick_ds18b20_conversion();

    while (true) {
        update_ambient_readings();
        ensure_ds18b20_devices();
        read_ds18b20_temperatures();
        kick_ds18b20_conversion();
        vTaskDelay(pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}

/**
 * @brief Spawn the sensor acquisition task.
 *
 * @param model Pointer to the data model to populate.
 * @param bus Shared OneWire bus handle for DS18B20 devices.
 * @return void
 */
void sensors_task_start(sensor_data_model_t *model, onewire_bus_handle_t bus)
{
    s_ctx.model = model;
    s_ctx.bus = bus;
    xTaskCreatePinnedToCore(sensors_task, "t_sensors", 6144, NULL, 5, NULL, 1);
}
