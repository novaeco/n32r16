#include "io_map.h"

#include "sdkconfig.h"

static const io_map_t io_map = {
#if CONFIG_SENSOR_AMBIENT_SENSOR_SHT20
    .ambient = {
        {.type = IO_AMBIENT_SENSOR_SHT20, .address = 0x40},
        {.type = IO_AMBIENT_SENSOR_SHT20, .address = 0x40},
    },
    .ambient_count = 2,
#elif CONFIG_SENSOR_AMBIENT_SENSOR_BME280
    .ambient = {
        {.type = IO_AMBIENT_SENSOR_BME280, .address = 0x76},
        {.type = IO_AMBIENT_SENSOR_BME280, .address = 0x77},
    },
    .ambient_count = 2,
#else
    .ambient = {
        {.type = IO_AMBIENT_SENSOR_NONE, .address = 0},
        {.type = IO_AMBIENT_SENSOR_NONE, .address = 0},
    },
    .ambient_count = 0,
#endif
#if CONFIG_SENSOR_PWM_BACKEND_DRIVER_PCA9685
    .pwm_backend = IO_PWM_BACKEND_PCA9685,
    .pca9685_address = 0x41,
#elif CONFIG_SENSOR_PWM_BACKEND_DRIVER_TLC5947
    .pwm_backend = IO_PWM_BACKEND_TLC5947,
    .pca9685_address = 0,
#else
    .pwm_backend = IO_PWM_BACKEND_NONE,
    .pca9685_address = 0,
#endif
    .mcp23017_addresses = {0x20, 0x21},
};

const io_map_t *io_map_get(void)
{
    return &io_map;
}
