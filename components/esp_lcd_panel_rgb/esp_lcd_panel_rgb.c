#include "esp_lcd_panel_rgb.h"

#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

static const char *TAG = "rgb_panel";

esp_err_t esp_lcd_new_panel_rgb_default(const esp_lcd_rgb_panel_helper_config_t *config,
                                        esp_lcd_panel_handle_t *panel_handle)
{
    if (!config || !panel_handle || config->data_gpio_nums_count != 16) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_lcd_rgb_panel_config_t cfg = {
        .data_gpio_nums = config->data_gpio_nums,
        .bits_per_pixel = 16,
        .hsync_gpio_num = config->hsync_gpio_num,
        .vsync_gpio_num = config->vsync_gpio_num,
        .de_gpio_num = config->de_gpio_num,
        .pclk_gpio_num = config->pclk_gpio_num,
        .disp_gpio_num = config->disp_gpio_num,
        .timings = {
            .pclk_hz = config->pclk_hz,
            .h_res = 1024,
            .v_res = 600,
            .hsync_back_porch = config->hsync_back_porch,
            .hsync_front_porch = config->hsync_front_porch,
            .hsync_pulse_width = config->hsync_pulse_width,
            .vsync_back_porch = config->vsync_back_porch,
            .vsync_front_porch = config->vsync_front_porch,
            .vsync_pulse_width = config->vsync_pulse_width,
            .flags = {
                .hsync_idle_low = config->hsync_polarity,
                .vsync_idle_low = config->vsync_polarity,
                .de_idle_high = config->de_polarity,
                .pclk_active_neg = config->pclk_active_neg,
            },
        },
    };

    esp_lcd_panel_handle_t panel;
    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_panel_reset(panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel reset failed: %s", esp_err_to_name(err));
        esp_lcd_panel_del(panel);
        return err;
    }

    err = esp_lcd_panel_init(panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed: %s", esp_err_to_name(err));
        esp_lcd_panel_del(panel);
        return err;
    }

    *panel_handle = panel;
    return ESP_OK;
}
