#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "drivers/ds18b20.h"
#include "drivers/mcp23017.h"
#include "drivers/pca9685.h"
#include "drivers/sht20.h"

typedef enum {
    NETWORK_STATE_DOWN = 0,
    NETWORK_STATE_PROVISIONING,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_READY,
    NETWORK_STATE_ERROR,
} network_state_t;

typedef struct {
    sht20_sample_t sht20[2];
    ds18b20_sample_t ds18b20[DS18B20_MAX_SENSORS];
    size_t ds18b20_count;
    mcp23017_state_t gpio[2];
    pca9685_state_t pwm;
    uint32_t heartbeat_counter;
    struct {
        network_state_t wifi;
        network_state_t websocket;
        uint8_t websocket_clients;
        bool time_synchronized;
    } network;
} data_model_snapshot_t;

typedef enum {
    IO_CMD_SET_PWM,
    IO_CMD_WRITE_GPIO,
} io_command_type_t;

typedef struct {
    io_command_type_t type;
    union {
        struct {
            uint8_t channel;
            uint16_t duty;
        } pwm;
        struct {
            uint8_t device;
            uint8_t port;
            uint8_t mask;
            uint8_t value;
        } gpio;
    } data;
} io_command_t;

void data_model_init(void);
void data_model_set_sht20(size_t index, const sht20_sample_t *sample);
void data_model_set_ds18b20(const ds18b20_sample_t *samples, size_t count);
void data_model_set_gpio(uint8_t device_index, const mcp23017_state_t *state);
void data_model_set_pwm(const pca9685_state_t *state);
void data_model_set_wifi_state(network_state_t state);
void data_model_set_ws_state(network_state_t state, uint8_t client_count);
void data_model_set_time_synchronized(bool synchronized);
bool data_model_snapshot_copy(data_model_snapshot_t *out_snapshot);
bool data_model_format_sensor_update(char *buffer, size_t capacity, size_t *out_len, uint32_t *crc_out);
bool data_model_parse_command(const uint8_t *buffer, size_t len, io_command_t *cmd);

