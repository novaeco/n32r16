#include "display/lvgl_port.h"

#include "board_waveshare7b_pins.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "common/util/memory_profile.h"
#include <stdlib.h>

static const char *TAG = "lvgl_port";

static SemaphoreHandle_t s_lvgl_mutex;
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_color_t *s_buf1;
static lv_color_t *s_buf2;
static esp_lcd_panel_handle_t s_panel;

static bool display_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2 + 1, y2 + 1, color_p);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel draw failed: %s", esp_err_to_name(err));
    }
    lv_disp_flush_ready(drv);
    return false;
}

static void lvgl_tick(void *arg)
{
    (void)arg;
    lv_tick_inc(5);
}

esp_err_t lvgl_port_init(void)
{
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_lvgl_mutex) {
        return ESP_ERR_NO_MEM;
    }

    esp_lcd_rgb_panel_helper_config_t cfg = {
        .de_gpio_num = HMI_LCD_DE,
        .pclk_gpio_num = HMI_LCD_PCLK,
        .hsync_gpio_num = HMI_LCD_HSYNC,
        .vsync_gpio_num = HMI_LCD_VSYNC,
        .data_gpio_nums = HMI_LCD_DATA_PINS,
        .data_gpio_nums_count = 16,
        .disp_gpio_num = HMI_LCD_DISP,
        .hsync_back_porch = 20,
        .hsync_front_porch = 160,
        .hsync_pulse_width = 20,
        .vsync_back_porch = 12,
        .vsync_front_porch = 23,
        .vsync_pulse_width = 10,
        .pclk_hz = 50000000,
        .pclk_active_neg = true,
        .hsync_polarity = false,
        .vsync_polarity = false,
        .de_polarity = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_rgb_default(&cfg, &s_panel), TAG, "panel");

    lv_init();

    size_t draw_buf_pixels = memory_profile_recommend_draw_buffer_px(1024, 600, 40);
    s_buf1 = heap_caps_malloc(draw_buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_buf2 = heap_caps_malloc(draw_buf_pixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_buf1 || !s_buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL buffers");
        return ESP_ERR_NO_MEM;
    }
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, draw_buf_pixels);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = 1024;
    s_disp_drv.ver_res = 600;
    s_disp_drv.flush_cb = display_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    const esp_timer_create_args_t periodic_timer_args = {
        .callback = lvgl_tick,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 5000));
    return ESP_OK;
}

void lvgl_port_lock(void)
{
    xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY);
}

void lvgl_port_unlock(void)
{
    xSemaphoreGiveRecursive(s_lvgl_mutex);
}

void lvgl_port_flush_ready(void)
{
    lv_timer_handler();
}
