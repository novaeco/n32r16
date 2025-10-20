#include "esp_lcd_panel_rgb_driver.h"

#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"

static const char *TAG = "rgb_panel";

esp_err_t rgb_panel_create(const rgb_panel_config_t *cfg, esp_lcd_panel_handle_t *handle) {
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is null");
    ESP_RETURN_ON_FALSE(cfg != NULL, ESP_ERR_INVALID_ARG, TAG, "cfg is null");

    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = cfg->data_width,
        .psram_trans_align = 64,
        .clk_src = LCD_CLK_SRC_PLL160M,
        .disp_gpio_num = GPIO_NUM_NC,
        .pclk_gpio_num = cfg->pclk_gpio_num,
        .vsync_gpio_num = cfg->vsync_gpio_num,
        .hsync_gpio_num = cfg->hsync_gpio_num,
        .de_gpio_num = cfg->de_gpio_num,
        .timings = {
            .pclk_hz = cfg->pclk_hz,
            .h_res = cfg->h_res,
            .v_res = cfg->v_res,
            .hsync_pulse_width = cfg->hsync_pulse_width,
            .hsync_back_porch = cfg->hsync_back_porch,
            .hsync_front_porch = cfg->hsync_front_porch,
            .vsync_pulse_width = cfg->vsync_pulse_width,
            .vsync_back_porch = cfg->vsync_back_porch,
            .vsync_front_porch = cfg->vsync_front_porch,
            .flags = {
                .pclk_active_neg = cfg->pclk_active_neg,
                .hsync_idle_high = cfg->hsync_idle_high,
                .vsync_idle_high = cfg->vsync_idle_high,
                .de_idle_high = cfg->de_idle_high,
            },
        },
        .flags = {
            .fb_in_psram = true,
            .double_fb = true,
        },
    };

    for (int i = 0; i < cfg->data_width; ++i) {
        panel_config.data_gpio_nums[i] = cfg->data_gpio_nums[i];
    }

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RGB panel: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

