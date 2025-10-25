#pragma once

#include "driver/gpio.h"
#include "sdkconfig.h"

#define SENSOR_NODE_I2C_SCL GPIO_NUM_18
#define SENSOR_NODE_I2C_SDA GPIO_NUM_17
#define SENSOR_NODE_ONEWIRE_PIN ((gpio_num_t)CONFIG_SENSOR_ONEWIRE_GPIO)

#define SENSOR_NODE_STATUS_LED GPIO_NUM_2
