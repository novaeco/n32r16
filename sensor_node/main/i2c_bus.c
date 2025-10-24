#include "i2c_bus.h"

#include "board_pins.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "i2c_bus";

esp_err_t i2c_bus_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SENSOR_NODE_I2C_SDA,
        .scl_io_num = SENSOR_NODE_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM_0, &conf), TAG, "param config");
    return i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
}

static esp_err_t i2c_master_cmd(uint8_t addr, const uint8_t *wr_data, size_t wr_len, uint8_t *rd_data,
                                size_t rd_len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    esp_err_t err = i2c_master_start(cmd);
    if (err != ESP_OK) {
        i2c_cmd_link_delete(cmd);
        return err;
    }
    if (wr_len > 0) {
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        if (wr_data && wr_len) {
            i2c_master_write(cmd, (uint8_t *)wr_data, wr_len, true);
        }
    }
    if (rd_len > 0) {
        if (wr_len > 0) {
            i2c_master_start(cmd);
        }
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
        if (rd_len > 1) {
            i2c_master_read(cmd, rd_data, rd_len - 1, I2C_MASTER_ACK);
        }
        i2c_master_read_byte(cmd, rd_data + rd_len - 1, I2C_MASTER_NACK);
    }
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C cmd failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t i2c_bus_read(uint8_t addr, uint8_t *data, size_t len)
{
    return i2c_master_cmd(addr, NULL, 0, data, len);
}

esp_err_t i2c_bus_write(uint8_t addr, const uint8_t *data, size_t len)
{
    return i2c_master_cmd(addr, data, len, NULL, 0);
}

esp_err_t i2c_bus_write_read(uint8_t addr, const uint8_t *wr_data, size_t wr_len, uint8_t *rd_data,
                             size_t rd_len)
{
    return i2c_master_cmd(addr, wr_data, wr_len, rd_data, rd_len);
}
