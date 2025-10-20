#include "tasks/t_heartbeat.h"

#include "board_pins.h"
#include "data_model.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HEARTBEAT_TASK_STACK 2048
#define HEARTBEAT_TASK_PRIO 4

static void heartbeat_task(void *arg) {
    (void)arg;
    gpio_set_direction(HEARTBEAT_LED_GPIO, GPIO_MODE_OUTPUT);
    bool level = false;
    while (true) {
        level = !level;
        gpio_set_level(HEARTBEAT_LED_GPIO, level);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void t_heartbeat_start(void) {
    xTaskCreate(heartbeat_task, "t_heartbeat", HEARTBEAT_TASK_STACK, NULL, HEARTBEAT_TASK_PRIO,
                NULL);
}

