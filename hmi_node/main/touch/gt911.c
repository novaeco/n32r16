#include "touch/gt911.h"

#include <string.h>

#include "board_waveshare7b_pins.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define GT911_ADDR_PRIMARY 0x5D
#define GT911_ADDR_SECONDARY 0x14
#define GT911_REG_STATUS 0x814E
#define GT911_REG_POINTS 0x8150
#define GT911_REG_CONFIG 0x8047

static const char *TAG = "gt911";
static uint8_t s_gt911_addr = GT911_ADDR_PRIMARY;
static lv_indev_t *s_input_dev;

static esp_err_t gt911_write(uint16_t reg, const uint8_t *data, size_t len) {
    uint8_t buf[2];
    buf[0] = reg & 0xFF;
    buf[1] = reg >> 8;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_gt911_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, buf, sizeof(buf), true);
    if (len > 0) {
        i2c_master_write(cmd, (uint8_t *)data, len, true);
    }
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t gt911_read(uint16_t reg, uint8_t *data, size_t len) {
    uint8_t buf[2];
    buf[0] = reg & 0xFF;
    buf[1] = reg >> 8;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_gt911_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, buf, sizeof(buf), true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_gt911_addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

static void gt911_reset_sequence(void) {
    gpio_set_direction(TOUCH_INT_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(TOUCH_RST_GPIO, GPIO_MODE_OUTPUT);

    gpio_set_level(TOUCH_INT_GPIO, 0);
    gpio_set_level(TOUCH_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(TOUCH_INT_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_direction(TOUCH_INT_GPIO, GPIO_MODE_INPUT);
}

static esp_err_t gt911_detect(void) {
    uint8_t config[4];
    if (gt911_read(GT911_REG_CONFIG, config, sizeof(config)) == ESP_OK) {
        ESP_LOGI(TAG, "GT911 detected at 0x%02X", s_gt911_addr);
        return ESP_OK;
    }
    if (s_gt911_addr == GT911_ADDR_PRIMARY) {
        s_gt911_addr = GT911_ADDR_SECONDARY;
        return gt911_read(GT911_REG_CONFIG, config, sizeof(config));
    }
    return ESP_FAIL;
}

esp_err_t gt911_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_SDA_GPIO,
        .scl_io_num = TOUCH_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = 400000,
        },
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0));

    gt911_reset_sequence();
    if (gt911_detect() != ESP_OK) {
        ESP_LOGE(TAG, "GT911 not found");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void gt911_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    uint8_t status = 0;
    if (gt911_read(GT911_REG_STATUS, &status, 1) != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    if ((status & 0x80) == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint8_t buf[8];
    if (gt911_read(GT911_REG_POINTS, buf, sizeof(buf)) != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    uint16_t x = buf[1] << 8 | buf[0];
    uint16_t y = buf[3] << 8 | buf[2];

    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;

    uint8_t clear = 0;
    gt911_write(GT911_REG_STATUS, &clear, 1);
}

void gt911_register_input_device(void) {
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = gt911_read_cb;
    s_input_dev = lv_indev_drv_register(&indev_drv);
    (void)s_input_dev;
}

