#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_interface.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int de_gpio_num;
    int pclk_gpio_num;
    int hsync_gpio_num;
    int vsync_gpio_num;
    const int *data_gpio_nums;
    size_t data_gpio_nums_count;
    int disp_gpio_num;
    int hsync_back_porch;
    int hsync_front_porch;
    int hsync_pulse_width;
    int vsync_back_porch;
    int vsync_front_porch;
    int vsync_pulse_width;
    uint32_t pclk_hz;
    bool pclk_active_neg;
    bool hsync_polarity;
    bool vsync_polarity;
    bool de_polarity;
} esp_lcd_rgb_panel_helper_config_t;

esp_err_t esp_lcd_new_panel_rgb_default(const esp_lcd_rgb_panel_helper_config_t *config,
                                        esp_lcd_panel_handle_t *panel_handle);

#ifdef __cplusplus
}
#endif
