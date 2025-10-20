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
#include "time_sync.h"

#define SENSORS_TASK_STACK 6144
#define SENSORS_TASK_PRIO 5
#define SHT20_READ_INTERVAL_MS 200U
#define DS18B20_RESCAN_MS 60000U
#define LOOP_PERIOD_MS 50U
#define SHT20_RETRY_MAX 3

static const char *TAG = "t_sensors";

typedef enum {
    DS_STATE_IDLE,
    DS_STATE_CONVERTING,
} ds_state_t;

static bool read_sht20_sensor(uint8_t address, sht20_sample_t *out_sample) {
    for (int attempt = 0; attempt < SHT20_RETRY_MAX; ++attempt) {
        esp_err_t err = sht20_read(address, out_sample);
        if (err == ESP_OK && out_sample->valid) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(20 * (attempt + 1)));
    }
    ESP_LOGW(TAG, "SHT20 0x%02X read failed", address);
    out_sample->valid = false;
    return false;
}

static void sensors_task(void *arg) {
    (void)arg;
    onewire_bus_init(ONEWIRE_GPIO);

    uint8_t sht20_addresses[2] = {0x40, 0x40};
    for (size_t i = 0; i < 2; ++i) {
        sht20_init(sht20_addresses[i]);
    }

    ds18b20_sample_t ds_samples[DS18B20_MAX_SENSORS] = {0};
    size_t ds_count = 0;
    if (ds18b20_scan(ds_samples, &ds_count) == ESP_OK) {
        data_model_set_ds18b20(ds_samples, ds_count);
    }

    TickType_t last_sht20_tick = xTaskGetTickCount();
    uint64_t last_rescan_ms = time_sync_get_monotonic_ms();
    ds_state_t ds_state = DS_STATE_IDLE;
    uint64_t ds_deadline_ms = 0;

    while (true) {
        TickType_t now_tick = xTaskGetTickCount();
        uint64_t now_ms = time_sync_get_monotonic_ms();

        if ((now_tick - last_sht20_tick) >= pdMS_TO_TICKS(SHT20_READ_INTERVAL_MS)) {
            last_sht20_tick = now_tick;
            for (size_t i = 0; i < 2; ++i) {
                sht20_sample_t sample = {0};
                if (read_sht20_sensor(sht20_addresses[i], &sample)) {
                    data_model_set_sht20(i, &sample);
                }
            }
        }

        if (ds_state == DS_STATE_IDLE && ds_count > 0) {
            if (ds18b20_start_conversion() == ESP_OK) {
                ds_deadline_ms = now_ms + ds18b20_get_conversion_time_ms();
                ds_state = DS_STATE_CONVERTING;
            }
        } else if (ds_state == DS_STATE_CONVERTING && now_ms >= ds_deadline_ms) {
            if (ds18b20_read_scratchpad(ds_samples, ds_count) == ESP_OK) {
                data_model_set_ds18b20(ds_samples, ds_count);
            }
            ds_state = DS_STATE_IDLE;
        }

        if ((now_ms - last_rescan_ms) >= DS18B20_RESCAN_MS) {
            if (ds18b20_scan(ds_samples, &ds_count) == ESP_OK) {
                data_model_set_ds18b20(ds_samples, ds_count);
            }
            last_rescan_ms = now_ms;
        }

        sensor_ws_server_send_snapshot();
        vTaskDelay(pdMS_TO_TICKS(LOOP_PERIOD_MS));
    }
}

void t_sensors_start(void) {
    xTaskCreate(sensors_task, "t_sensors", SENSORS_TASK_STACK, NULL, SENSORS_TASK_PRIO, NULL);
}

