#pragma once

#include "driver/gpio.h"

#define HMI_LCD_DE GPIO_NUM_5
#define HMI_LCD_PCLK GPIO_NUM_7
#define HMI_LCD_HSYNC GPIO_NUM_46
#define HMI_LCD_VSYNC GPIO_NUM_3
#define HMI_LCD_DISP GPIO_NUM_6

static const int HMI_LCD_DATA_PINS[16] = {
    GPIO_NUM_42,
    GPIO_NUM_41,
    GPIO_NUM_40,
    GPIO_NUM_39,
    GPIO_NUM_45,
    GPIO_NUM_48,
    GPIO_NUM_47,
    GPIO_NUM_21,
    GPIO_NUM_14,
    GPIO_NUM_38,
    GPIO_NUM_18,
    GPIO_NUM_17,
    GPIO_NUM_10,
    GPIO_NUM_9,
    GPIO_NUM_4,
    GPIO_NUM_8,
};

#define HMI_TOUCH_SCL GPIO_NUM_9
#define HMI_TOUCH_SDA GPIO_NUM_8
#define HMI_TOUCH_INT GPIO_NUM_4
#define HMI_TOUCH_RST GPIO_NUM_2
