#include "lvgl_port_config.h"

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "lvgl_port";

static void lvgl_port_log_cb(lv_log_level_t level, const char *buf) {
    switch (level) {
        case LV_LOG_LEVEL_ERROR:
            ESP_LOGE(TAG, "%s", buf);
            break;
        case LV_LOG_LEVEL_WARN:
            ESP_LOGW(TAG, "%s", buf);
            break;
        case LV_LOG_LEVEL_INFO:
            ESP_LOGI(TAG, "%s", buf);
            break;
        default:
            ESP_LOGD(TAG, "%s", buf);
            break;
    }
}

void lvgl_port_configure_logging(void) {
    lv_log_register_print_cb(lvgl_port_log_cb);
}

uint32_t lvgl_port_get_default_dpi(void) {
    return 160;
}

