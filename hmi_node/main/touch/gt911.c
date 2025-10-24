#include "touch/gt911.h"

#include "board_waveshare7b_pins.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "gt911";

static uint8_t s_gt911_addr = 0x5D;

static esp_err_t gt911_i2c_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = HMI_TOUCH_SDA,
        .scl_io_num = HMI_TOUCH_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &conf));
    return i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
}

static esp_err_t gt911_write(uint16_t reg, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_gt911_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg & 0xFF, true);
    i2c_master_write_byte(cmd, (reg >> 8) & 0xFF, true);
    if (len) {
        i2c_master_write(cmd, (uint8_t *)data, len, true);
    }
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t gt911_read(uint16_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_gt911_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg & 0xFF, true);
    i2c_master_write_byte(cmd, (reg >> 8) & 0xFF, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_gt911_addr << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static void gt911_reset_sequence(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << HMI_TOUCH_RST) | (1ULL << HMI_TOUCH_INT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(HMI_TOUCH_RST, 0);
    gpio_set_level(HMI_TOUCH_INT, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(HMI_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

esp_err_t gt911_init(void)
{
    gt911_i2c_init();
    gt911_reset_sequence();
    uint8_t product_id[4];
    if (gt911_read(0x8140, product_id, sizeof(product_id)) != ESP_OK) {
        s_gt911_addr = 0x14;
        if (gt911_read(0x8140, product_id, sizeof(product_id)) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to communicate with GT911");
            return ESP_FAIL;
        }
    }
    ESP_LOGI(TAG, "GT911 ID: %02X%02X%02X%02X", product_id[0], product_id[1], product_id[2], product_id[3]);
    return ESP_OK;
}

bool gt911_read_touch(gt911_touch_data_t *data)
{
    if (!data) {
        return false;
    }
    uint8_t status = 0;
    if (gt911_read(0x814E, &status, 1) != ESP_OK) {
        return false;
    }
    bool has_data = status & 0x80;
    if (!has_data) {
        data->pressed = false;
        return false;
    }
    uint8_t buf[8] = {0};
    if (gt911_read(0x8150, buf, sizeof(buf)) != ESP_OK) {
        return false;
    }
    data->x = ((uint16_t)buf[1] << 8) | buf[0];
    data->y = ((uint16_t)buf[3] << 8) | buf[2];
    data->pressed = true;
    uint8_t clear = 0;
    gt911_write(0x814E, &clear, 1);
    return true;
}
