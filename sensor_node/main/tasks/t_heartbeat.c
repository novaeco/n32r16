#include "tasks/t_heartbeat.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief Blink the status LED to indicate the firmware heartbeat.
 *
 * @param arg Unused task argument.
 * @return void
 */
static void heartbeat_task(void *arg)
{
    (void)arg;
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << SENSOR_NODE_STATUS_LED,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&cfg);

    while (true) {
        gpio_set_level(SENSOR_NODE_STATUS_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(SENSOR_NODE_STATUS_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(900));
    }
}

/**
 * @brief Create the heartbeat task pinned to the application core.
 *
 * @return void
 */
void heartbeat_task_start(void)
{
    xTaskCreatePinnedToCore(heartbeat_task, "t_heartbeat", 2048, NULL, 1, NULL, 0);
}
