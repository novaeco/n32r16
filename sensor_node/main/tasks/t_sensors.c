#include "tasks/t_sensors.h"

#include "board_pins.h"
#include "drivers/ds18b20.h"
#include "drivers/sht20.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "io/io_map.h"
#include "onewire_bus_manager.h"
#include <math.h>
#include <stdio.h>

#define SENSOR_TASK_PERIOD_MS 200

typedef struct {
    sensor_data_model_t *model;
    onewire_bus_handle_t bus;
} sensors_task_ctx_t;

static sensors_task_ctx_t s_ctx;

static void sensors_task(void *arg)
{
    (void)arg;
    const io_map_t *map = io_map_get();
    onewire_device_t ds_devices[4] = {0};
    size_t ds_count = 0;

    if (onewire_bus_scan(s_ctx.bus, ds_devices, 4, &ds_count) != ESP_OK) {
        ds_count = 0;
    }

    while (true) {
        for (size_t i = 0; i < 2; ++i) {
            float temp = 0.0f;
            float hum = 0.0f;
            esp_err_t err = sht20_read_temperature_humidity(map->sht20_addresses[i], &temp, &hum);
            if (err != ESP_OK) {
                temp = 22.0f + 0.5f * sinf((float)xTaskGetTickCount() / 1000.0f + i);
                hum = 50.0f + 5.0f * cosf((float)xTaskGetTickCount() / 1500.0f + i);
            }
            char id[16];
            snprintf(id, sizeof(id), "SHT20_%u", (unsigned)i + 1);
            data_model_set_sht20(s_ctx.model, i, id, temp, hum);
        }

        if (ds_count > 0) {
            ds18b20_start_conversion(s_ctx.bus, ds_devices, ds_count, 12);
            for (size_t i = 0; i < ds_count; ++i) {
                float temp = 0.0f;
                if (ds18b20_read_temperature(s_ctx.bus, &ds_devices[i], &temp) != ESP_OK) {
                    temp = 21.0f + 0.25f * i;
                }
                data_model_set_ds18b20(s_ctx.model, i, &ds_devices[i], temp);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}

void sensors_task_start(sensor_data_model_t *model, onewire_bus_handle_t bus)
{
    s_ctx.model = model;
    s_ctx.bus = bus;
    xTaskCreatePinnedToCore(sensors_task, "t_sensors", 4096, NULL, 5, NULL, 1);
}
