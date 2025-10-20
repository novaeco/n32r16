#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_interface.h"

typedef struct {
    int data_width;
    uint32_t pclk_hz;
    int h_res;
    int v_res;
    bool de_idle_high;
    bool pclk_active_neg;
    bool hsync_idle_high;
    bool vsync_idle_high;
    int hsync_pulse_width;
    int hsync_back_porch;
    int hsync_front_porch;
    int vsync_pulse_width;
    int vsync_back_porch;
    int vsync_front_porch;
    int data_gpio_nums[16];
    int de_gpio_num;
    int hsync_gpio_num;
    int vsync_gpio_num;
    int pclk_gpio_num;
} rgb_panel_config_t;

esp_err_t rgb_panel_create(const rgb_panel_config_t *cfg, esp_lcd_panel_handle_t *handle);

