#include "tasks/t_sensors.h"

#include "board_pins.h"
#include "data_model.h"
#include "drivers/ds18b20.h"
#include "drivers/sht20.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "net/ws_server.h"
#include "onewire_bus.h"

#define SENSORS_TASK_STACK 4096
#define SENSORS_TASK_PRIO 5

static const char *TAG = "t_sensors";

static void sensors_task(void *arg) {
    (void)arg;
    onewire_bus_init(ONEWIRE_GPIO);

    uint8_t sht20_addresses[2] = {0x40, 0x40};
    sht20_sample_t sht_samples[2] = {0};
    uint8_t sht_fail_count[2] = {0};
    for (size_t i = 0; i < 2; ++i) {
        if (sht20_init(sht20_addresses[i]) != ESP_OK) {
            ESP_LOGW(TAG, "SHT20 %zu init failed", i);
        }
    }

    ds18b20_sample_t ds_samples[DS18B20_MAX_SENSORS] = {0};
    size_t ds_count = 0;
    if (ds18b20_scan(ds_samples, &ds_count) == ESP_OK) {
        data_model_set_ds18b20(ds_samples, ds_count);
    }
    TickType_t last_rescan = xTaskGetTickCount();
    const TickType_t rescan_period = pdMS_TO_TICKS(10000);

    const TickType_t period = pdMS_TO_TICKS(200);

    while (true) {
        for (size_t i = 0; i < 2; ++i) {
            if (sht20_read(sht20_addresses[i], &sht_samples[i]) == ESP_OK) {
                sht_fail_count[i] = 0;
                data_model_set_sht20(i, &sht_samples[i]);
            } else {
                sht_samples[i].valid = false;
                data_model_set_sht20(i, &sht_samples[i]);
                if (++sht_fail_count[i] >= 5) {
                    ESP_LOGW(TAG, "Reinitializing SHT20 %zu after consecutive failures", i);
                    sht20_init(sht20_addresses[i]);
                    sht_fail_count[i] = 0;
                }
            }
        }

        bool ds_valid = ds_count > 0;
        if (ds_valid && ds18b20_trigger_conversion() == ESP_OK) {
            if (ds18b20_read_scratchpad(ds_samples, ds_count) == ESP_OK) {
                data_model_set_ds18b20(ds_samples, ds_count);
            } else {
                ds_valid = false;
            }
        } else if (ds_count > 0) {
            ds_valid = false;
        }

        if (!ds_valid && ds_count > 0) {
            for (size_t i = 0; i < ds_count; ++i) {
                ds_samples[i].valid = false;
            }
            data_model_set_ds18b20(ds_samples, ds_count);
        }

        TickType_t now = xTaskGetTickCount();
        if (!ds_valid || (now - last_rescan) > rescan_period) {
            if (ds18b20_scan(ds_samples, &ds_count) == ESP_OK) {
                data_model_set_ds18b20(ds_samples, ds_count);
            } else {
                ESP_LOGW(TAG, "DS18B20 rescan failed");
                ds_count = 0;
                data_model_set_ds18b20(ds_samples, 0);
            }
            last_rescan = now;
        }

        sensor_ws_server_send_snapshot();
        vTaskDelay(period);
    }
}

void t_sensors_start(void) {
    xTaskCreate(sensors_task, "t_sensors", SENSORS_TASK_STACK, NULL, SENSORS_TASK_PRIO, NULL);
}

