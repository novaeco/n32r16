#pragma once

#include <stdint.h>

#define IO_MAX_AMBIENT_SENSORS 2U
#define IO_MUX_CHANNEL_NONE (-1)

typedef enum {
    IO_AMBIENT_SENSOR_NONE = 0,
    IO_AMBIENT_SENSOR_SHT20,
    IO_AMBIENT_SENSOR_BME280,
} io_ambient_sensor_type_t;

typedef enum {
    IO_PWM_BACKEND_NONE = 0,
    IO_PWM_BACKEND_PCA9685,
    IO_PWM_BACKEND_TLC5947,
} io_pwm_backend_t;

typedef struct {
    io_ambient_sensor_type_t type;
    uint8_t address;
    uint8_t mux_address;
    int8_t mux_channel;
} io_ambient_sensor_t;

typedef struct {
    io_ambient_sensor_t ambient[IO_MAX_AMBIENT_SENSORS];
    uint8_t ambient_count;
    io_pwm_backend_t pwm_backend;
    uint8_t pca9685_address;
    uint8_t mcp23017_addresses[2];
} io_map_t;

const io_map_t *io_map_get(void);
