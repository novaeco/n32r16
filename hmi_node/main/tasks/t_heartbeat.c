#include "tasks/t_heartbeat.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HMI_HEARTBEAT_GPIO GPIO_NUM_1
#define HEARTBEAT_STACK 2048
#define HEARTBEAT_PRIO 3

static void heartbeat_task(void *arg) {
    (void)arg;
    gpio_set_direction(HMI_HEARTBEAT_GPIO, GPIO_MODE_OUTPUT);
    bool level = false;
    while (true) {
        level = !level;
        gpio_set_level(HMI_HEARTBEAT_GPIO, level);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void t_hmi_heartbeat_start(void) {
    xTaskCreate(heartbeat_task, "t_hb", HEARTBEAT_STACK, NULL, HEARTBEAT_PRIO, NULL);
}

