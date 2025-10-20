#include "display/lvgl_port.h"

#include <stdlib.h>

#include "board_waveshare7b_pins.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb_driver.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl_port_config.h"

#define LVGL_TICK_PERIOD_MS 5

static const char *TAG = "lvgl_port";
static SemaphoreHandle_t s_lvgl_mutex;
static lv_display_t *s_display;
static esp_lcd_panel_handle_t s_panel_handle;
static esp_timer_handle_t s_tick_timer;

static void lvgl_tick_cb(void *arg) {
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    esp_lcd_panel_draw_bitmap(s_panel_handle, x1, y1, x2 + 1, y2 + 1, px_map);
    lv_display_flush_ready(disp);
}

esp_err_t lvgl_port_init(void) {
    if (s_display != NULL) {
        return ESP_OK;
    }

    lv_init();
    lvgl_port_configure_logging();

    s_lvgl_mutex = xSemaphoreCreateMutex();
    if (s_lvgl_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    rgb_panel_config_t panel_cfg = {
        .data_width = 16,
        .pclk_hz = 50000000,
        .h_res = 1024,
        .v_res = 600,
        .de_idle_high = false,
        .pclk_active_neg = true,
        .hsync_idle_high = false,
        .vsync_idle_high = false,
        .hsync_pulse_width = 20,
        .hsync_back_porch = 160,
        .hsync_front_porch = 140,
        .vsync_pulse_width = 3,
        .vsync_back_porch = 23,
        .vsync_front_porch = 12,
        .de_gpio_num = LCD_DE_GPIO,
        .hsync_gpio_num = LCD_HSYNC_GPIO,
        .vsync_gpio_num = LCD_VSYNC_GPIO,
        .pclk_gpio_num = LCD_PCLK_GPIO,
    };

    int data_pins[16] = {
        LCD_DATA0_GPIO,  LCD_DATA1_GPIO,  LCD_DATA2_GPIO,  LCD_DATA3_GPIO,
        LCD_DATA4_GPIO,  LCD_DATA5_GPIO,  LCD_DATA6_GPIO,  LCD_DATA7_GPIO,
        LCD_DATA8_GPIO,  LCD_DATA9_GPIO,  LCD_DATA10_GPIO, LCD_DATA11_GPIO,
        LCD_DATA12_GPIO, LCD_DATA13_GPIO, LCD_DATA14_GPIO, LCD_DATA15_GPIO,
    };
    for (int i = 0; i < 16; ++i) {
        panel_cfg.data_gpio_nums[i] = data_pins[i];
    }

    ESP_ERROR_CHECK(rgb_panel_create(&panel_cfg, &s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    size_t buf_size = panel_cfg.h_res * 40;
    lv_color_t *buf1 = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lv_color_t *buf2 = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "Display buffer allocation failed");
        return ESP_ERR_NO_MEM;
    }

    s_display = lv_display_create(panel_cfg.h_res, panel_cfg.v_res);
    lv_display_set_flush_cb(s_display, lvgl_flush_cb);
    lv_display_set_draw_buffers(s_display, buf1, buf2, buf_size * sizeof(lv_color_t),
                                LV_DISPLAY_RENDER_MODE_PARTIAL);

    const esp_timer_create_args_t timer_args = {
        .callback = lvgl_tick_cb,
        .arg = NULL,
        .name = "lvgl_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "LVGL initialized");
    return ESP_OK;
}

void lvgl_port_lock(void) {
    if (s_lvgl_mutex != NULL) {
        xSemaphoreTake(s_lvgl_mutex, portMAX_DELAY);
    }
}

void lvgl_port_unlock(void) {
    if (s_lvgl_mutex != NULL) {
        xSemaphoreGive(s_lvgl_mutex);
    }
}

