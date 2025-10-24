#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTO_MAX_COMMAND_SIZE 512U

typedef struct {
    char id[16];
    float temperature_c;
    float humidity_percent;
} proto_sht20_reading_t;

typedef struct {
    uint8_t rom_code[8];
    float temperature_c;
} proto_ds18b20_reading_t;

typedef struct {
    uint16_t port_a;
    uint16_t port_b;
} proto_mcp23017_state_t;

typedef struct {
    uint16_t frequency_hz;
    uint16_t duty_cycle[16];
} proto_pca9685_state_t;

typedef struct {
    uint32_t timestamp_ms;
    uint32_t sequence_id;
    size_t sht20_count;
    proto_sht20_reading_t sht20[2];
    size_t ds18b20_count;
    proto_ds18b20_reading_t ds18b20[4];
    proto_mcp23017_state_t mcp[2];
    proto_pca9685_state_t pwm;
} proto_sensor_update_t;

typedef struct {
    uint32_t timestamp_ms;
    uint32_t sequence_id;
    bool has_pwm_update;
    struct {
        uint8_t channel;
        uint16_t duty_cycle;
    } pwm_update;
    bool has_pwm_frequency;
    uint16_t pwm_frequency;
    bool has_gpio_write;
    struct {
        uint8_t device_index;
        uint8_t port; /* 0 -> A, 1 -> B */
        uint16_t mask;
        uint16_t value;
    } gpio_write;
} proto_command_t;

bool proto_encode_sensor_update_into(const proto_sensor_update_t *msg, bool use_cbor, uint8_t *buffer,
                                     size_t *buffer_len, uint32_t *crc32);
bool proto_encode_command_into(const proto_command_t *msg, bool use_cbor, uint8_t *buffer,
                               size_t *buffer_len, uint32_t *crc32);
bool proto_decode_command(const uint8_t *payload, size_t payload_len, bool is_cbor,
                          proto_command_t *out_msg, uint32_t expected_crc32);
bool proto_decode_sensor_update(const uint8_t *payload, size_t payload_len, bool is_cbor,
                                proto_sensor_update_t *out_msg, uint32_t expected_crc32);

