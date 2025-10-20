#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/ds18b20.h"
#include "drivers/mcp23017.h"
#include "drivers/pca9685.h"
#include "drivers/sht20.h"
#include "proto_codec.h"

typedef struct {
    float cpu_load;
    int8_t wifi_rssi;
    uint32_t uptime_ms;
    uint32_t i2c_errors;
    uint32_t onewire_errors;
} system_metrics_t;

typedef struct {
    sht20_sample_t sht20[2];
    ds18b20_sample_t ds18b20[DS18B20_MAX_SENSORS];
    size_t ds18b20_count;
    mcp23017_state_t gpio[2];
    pca9685_state_t pwm;
    system_metrics_t metrics;
    uint32_t last_crc;
    uint32_t last_seq_id;
} data_model_snapshot_t;

typedef enum {
    IO_CMD_SET_PWM,
    IO_CMD_WRITE_GPIO,
    IO_CMD_SET_PWM_FREQ,
} io_command_type_t;

typedef struct {
    io_command_type_t type;
    uint32_t seq_id;
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
        struct {
            uint16_t frequency_hz;
        } pwm_freq;
    } data;
} io_command_t;

void data_model_init(void);
void data_model_set_sht20(size_t index, const sht20_sample_t *sample);
void data_model_set_ds18b20(const ds18b20_sample_t *samples, size_t count);
void data_model_set_gpio(uint8_t device_index, const mcp23017_state_t *state);
void data_model_set_pwm(const pca9685_state_t *state);
void data_model_set_metrics(const system_metrics_t *metrics);
bool data_model_prepare_sensor_update(proto_buffer_t *out_buf);
bool data_model_build_heartbeat(proto_buffer_t *out_buf);
uint32_t data_model_next_sequence(void);
bool data_model_parse_command(const uint8_t *buffer, size_t len, io_command_t *cmd);
const data_model_snapshot_t *data_model_peek(void);

