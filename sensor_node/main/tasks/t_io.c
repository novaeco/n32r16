#include "tasks/t_io.h"

#include "drivers/mcp23017.h"
#include "drivers/pca9685.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "io/io_map.h"
#include <string.h>

typedef enum {
    IO_CMD_SET_PWM,
    IO_CMD_SET_PWM_FREQ,
    IO_CMD_WRITE_GPIO,
} io_command_type_t;

typedef struct {
    io_command_type_t type;
    union {
        struct {
            uint8_t channel;
            uint16_t duty;
        } pwm;
        uint16_t pwm_freq;
        struct {
            uint8_t device_index;
            uint8_t port;
            uint16_t mask;
            uint16_t value;
        } gpio;
    } data;
} io_command_t;

static QueueHandle_t s_cmd_queue;
static sensor_data_model_t *s_model;
static uint16_t s_pwm[16];
static uint16_t s_pwm_freq = 500;

static void io_task(void *arg)
{
    (void)arg;
    const io_map_t *map = io_map_get();

    pca9685_init(map->pca9685_address, s_pwm_freq);
    mcp23017_init(map->mcp23017_addresses[0], 0xFFFF, 0xFFFF);
    mcp23017_init(map->mcp23017_addresses[1], 0xFFFF, 0xFFFF);

    while (true) {
        io_command_t cmd;
        while (xQueueReceive(s_cmd_queue, &cmd, 0) == pdPASS) {
            switch (cmd.type) {
            case IO_CMD_SET_PWM:
                s_pwm[cmd.data.pwm.channel % 16] = cmd.data.pwm.duty;
                pca9685_set_pwm(map->pca9685_address, cmd.data.pwm.channel % 16, cmd.data.pwm.duty);
                break;
            case IO_CMD_SET_PWM_FREQ:
                s_pwm_freq = cmd.data.pwm_freq;
                pca9685_init(map->pca9685_address, s_pwm_freq);
                for (uint8_t i = 0; i < 16; ++i) {
                    pca9685_set_pwm(map->pca9685_address, i, s_pwm[i]);
                }
                break;
            case IO_CMD_WRITE_GPIO: {
                uint8_t idx = cmd.data.gpio.device_index % 2;
                uint16_t mask = (uint16_t)(cmd.data.gpio.mask & 0x00FFU);
                uint16_t value = (uint16_t)(cmd.data.gpio.value & 0x00FFU);
                uint8_t port = cmd.data.gpio.port ? 1U : 0U;
                if (port == 1U) {
                    mask <<= 8;
                    value <<= 8;
                }
                mcp23017_write_gpio(map->mcp23017_addresses[idx], mask, value);
                break;
            }
            }
        }

        for (uint8_t i = 0; i < 2; ++i) {
            uint16_t value = 0;
            if (mcp23017_read_gpio(map->mcp23017_addresses[i], &value) == ESP_OK) {
                data_model_set_gpio(s_model, i, value & 0xFF, (value >> 8) & 0xFF);
            }
        }
        data_model_set_pwm(s_model, s_pwm, 16, s_pwm_freq);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void io_task_start(sensor_data_model_t *model)
{
    s_model = model;
    memset(s_pwm, 0, sizeof(s_pwm));
    s_cmd_queue = xQueueCreate(16, sizeof(io_command_t));
    xTaskCreatePinnedToCore(io_task, "t_io", 4096, NULL, 5, NULL, 1);
}

bool io_task_set_pwm(uint8_t channel, uint16_t duty)
{
    io_command_t cmd = {
        .type = IO_CMD_SET_PWM,
        .data.pwm = {
            .channel = channel,
            .duty = duty,
        },
    };
    return xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(20)) == pdPASS;
}

bool io_task_set_pwm_frequency(uint16_t frequency_hz)
{
    io_command_t cmd = {
        .type = IO_CMD_SET_PWM_FREQ,
        .data.pwm_freq = frequency_hz,
    };
    return xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(20)) == pdPASS;
}

bool io_task_write_gpio(uint8_t device_index, uint8_t port, uint16_t mask, uint16_t value)
{
    io_command_t cmd = {
        .type = IO_CMD_WRITE_GPIO,
        .data.gpio = {
            .device_index = device_index,
            .port = port,
            .mask = mask,
            .value = value,
        },
    };
    return xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(20)) == pdPASS;
}
