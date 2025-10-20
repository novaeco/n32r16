#include "tasks/t_io.h"

#include "data_model.h"
#include "drivers/mcp23017.h"
#include "drivers/pca9685.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define IO_TASK_STACK 4096
#define IO_TASK_PRIO 5
#define IO_CMD_QUEUE_LEN 10

static QueueHandle_t s_cmd_queue;

static void apply_command(const io_command_t *cmd) {
    if (cmd->type == IO_CMD_SET_PWM) {
        pca9685_set_pwm(0x41, cmd->data.pwm.channel, cmd->data.pwm.duty);
    } else if (cmd->type == IO_CMD_WRITE_GPIO) {
        uint8_t address = cmd->data.gpio.device == 0 ? 0x20 : 0x21;
        mcp23017_write(address, cmd->data.gpio.port, cmd->data.gpio.mask, cmd->data.gpio.value);
    }
}

static void io_task(void *arg) {
    (void)arg;
    mcp23017_init(0x20, 0x0000);
    mcp23017_init(0x21, 0x0000);
    pca9685_init(0x41, 500);

    pca9685_state_t pwm_state;
    mcp23017_state_t gpio_state[2];

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(100);

    while (true) {
        io_command_t cmd;
        if (xQueueReceive(s_cmd_queue, &cmd, pdMS_TO_TICKS(10)) == pdPASS) {
            apply_command(&cmd);
        }

        if (xTaskGetTickCount() - last_wake >= period) {
            last_wake = xTaskGetTickCount();
            mcp23017_read(0x20, &gpio_state[0]);
            mcp23017_read(0x21, &gpio_state[1]);
            data_model_set_gpio(0, &gpio_state[0]);
            data_model_set_gpio(1, &gpio_state[1]);
            pca9685_snapshot(0x41, &pwm_state);
            data_model_set_pwm(&pwm_state);
        }
    }
}

void t_io_start(void) {
    s_cmd_queue = xQueueCreate(IO_CMD_QUEUE_LEN, sizeof(io_command_t));
    xTaskCreate(io_task, "t_io", IO_TASK_STACK, NULL, IO_TASK_PRIO, NULL);
}

bool t_io_post_command(const io_command_t *cmd) {
    if (s_cmd_queue == NULL || cmd == NULL) {
        return false;
    }
    return xQueueSend(s_cmd_queue, cmd, 0) == pdPASS;
}

