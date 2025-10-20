#include "tasks/t_sensors.h"

#include "board_pins.h"
#include "data_model.h"
#include "drivers/ds18b20.h"
#include "drivers/sht20.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "net/ws_server.h"
#include "onewire_bus.h"

#define SENSORS_TASK_STACK 4096
#define SENSORS_TASK_PRIO 5

static void sensors_task(void *arg) {
    (void)arg;
    onewire_bus_init(ONEWIRE_GPIO);

    uint8_t sht20_addresses[2] = {0x40, 0x40};
    sht20_sample_t sht_samples[2] = {0};
    for (size_t i = 0; i < 2; ++i) {
        sht20_init(sht20_addresses[i]);
    }

    ds18b20_sample_t ds_samples[DS18B20_MAX_SENSORS] = {0};
    size_t ds_count = 0;
    ds18b20_scan(ds_samples, &ds_count);
    data_model_set_ds18b20(ds_samples, ds_count);

    const TickType_t period = pdMS_TO_TICKS(200);

    while (true) {
        for (size_t i = 0; i < 2; ++i) {
            if (sht20_read(sht20_addresses[i], &sht_samples[i]) == ESP_OK) {
                data_model_set_sht20(i, &sht_samples[i]);
            }
        }

        if (ds_count > 0) {
            if (ds18b20_trigger_conversion() == ESP_OK) {
                ds18b20_read_scratchpad(ds_samples, ds_count);
                data_model_set_ds18b20(ds_samples, ds_count);
            }
        }

        sensor_ws_server_send_snapshot();
        vTaskDelay(period);
    }
}

void t_sensors_start(void) {
    xTaskCreate(sensors_task, "t_sensors", SENSORS_TASK_STACK, NULL, SENSORS_TASK_PRIO, NULL);
}

