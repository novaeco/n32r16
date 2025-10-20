#include "i2c_bus.h"

#include "esp_log.h"

esp_err_t i2c_bus_init(i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t frequency_hz) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = frequency_hz,
        },
    };
    ESP_ERROR_CHECK(i2c_param_config(port, &conf));
    return i2c_driver_install(port, conf.mode, 0, 0, 0);
}

esp_err_t i2c_bus_write_read(i2c_port_t port, uint8_t addr, const uint8_t *write_data,
                             size_t write_len, uint8_t *read_data, size_t read_len) {
    esp_err_t err = ESP_OK;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    if (write_len > 0) {
        i2c_master_write(cmd, (uint8_t *)write_data, write_len, true);
    }
    if (read_len > 0) {
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
        i2c_master_read(cmd, read_data, read_len, I2C_MASTER_LAST_NACK);
    }
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

