#include "tasks/t_io.h"

#include "data_model.h"
#include "drivers/mcp23017.h"
#include "drivers/pca9685.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "net/ws_server.h"

#define IO_TASK_STACK 4096
#define IO_TASK_PRIO 5
#define IO_CMD_QUEUE_LEN 10

static const char *TAG = "t_io";
static QueueHandle_t s_cmd_queue;

static esp_err_t apply_command(const io_command_t *cmd) {
    switch (cmd->type) {
        case IO_CMD_SET_PWM:
            if (cmd->data.pwm.channel >= 16U) {
                return ESP_ERR_INVALID_ARG;
            }
            return pca9685_set_pwm(0x41, cmd->data.pwm.channel, cmd->data.pwm.duty);
        case IO_CMD_WRITE_GPIO: {
            uint8_t address = cmd->data.gpio.device == 0 ? 0x20 : 0x21;
            return mcp23017_write(address, cmd->data.gpio.port, cmd->data.gpio.mask, cmd->data.gpio.value);
        }
        case IO_CMD_SET_PWM_FREQ:
            return pca9685_set_frequency(0x41, cmd->data.pwm_freq.frequency_hz);
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

static void io_task(void *arg) {
    (void)arg;
    ESP_ERROR_CHECK(mcp23017_init(0x20, 0x0000));
    ESP_ERROR_CHECK(mcp23017_init(0x21, 0x0000));
    ESP_ERROR_CHECK(pca9685_init(0x41, 500));

    pca9685_state_t pwm_state;
    mcp23017_state_t gpio_state[2];

    TickType_t last_snapshot = xTaskGetTickCount();
    const TickType_t snapshot_period = pdMS_TO_TICKS(100);

    while (true) {
        io_command_t cmd;
        if (xQueueReceive(s_cmd_queue, &cmd, pdMS_TO_TICKS(20)) == pdPASS) {
            esp_err_t err = apply_command(&cmd);
            if (err == ESP_OK) {
                (void)sensor_ws_server_send_ack(cmd.seq_id, true, NULL);
            } else {
                ESP_LOGW(TAG, "Command failed: %s", esp_err_to_name(err));
                (void)sensor_ws_server_send_ack(cmd.seq_id, false, esp_err_to_name(err));
            }
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_snapshot) >= snapshot_period) {
            last_snapshot = now;
            if (mcp23017_read(0x20, &gpio_state[0]) == ESP_OK) {
                data_model_set_gpio(0, &gpio_state[0]);
            }
            if (mcp23017_read(0x21, &gpio_state[1]) == ESP_OK) {
                data_model_set_gpio(1, &gpio_state[1]);
            }
            if (pca9685_snapshot(0x41, &pwm_state) == ESP_OK) {
                data_model_set_pwm(&pwm_state);
            }
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

