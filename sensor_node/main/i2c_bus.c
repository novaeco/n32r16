#include "i2c_bus.h"

#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "i2c_bus";
static i2c_port_t s_port = I2C_NUM_0;
static gpio_num_t s_sda;
static gpio_num_t s_scl;
static uint32_t s_frequency;
static uint32_t s_error_count;

static esp_err_t i2c_install_driver(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = s_sda,
        .scl_io_num = s_scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = s_frequency,
        },
    };
    ESP_ERROR_CHECK(i2c_param_config(s_port, &conf));
    return i2c_driver_install(s_port, conf.mode, 0, 0, 0);
}

static void i2c_bus_recover(void) {
    i2c_driver_delete(s_port);

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << s_scl) | (1ULL << s_sda),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    gpio_set_level(s_sda, 1);
    for (int i = 0; i < 9; ++i) {
        gpio_set_level(s_scl, 0);
        ets_delay_us(5);
        gpio_set_level(s_scl, 1);
        ets_delay_us(5);
    }
    gpio_set_level(s_sda, 0);
    ets_delay_us(5);
    gpio_set_level(s_scl, 1);
    ets_delay_us(5);
    gpio_set_level(s_sda, 1);

    i2c_install_driver();
}

esp_err_t i2c_bus_init(i2c_port_t port, gpio_num_t sda, gpio_num_t scl, uint32_t frequency_hz) {
    s_port = port;
    s_sda = sda;
    s_scl = scl;
    s_frequency = frequency_hz;
    s_error_count = 0;
    return i2c_install_driver();
}

esp_err_t i2c_bus_write_read(i2c_port_t port, uint8_t addr, const uint8_t *write_data,
                             size_t write_len, uint8_t *read_data, size_t read_len) {
    (void)port;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 2; ++attempt) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        if (write_len > 0 && write_data != NULL) {
            i2c_master_write(cmd, (uint8_t *)write_data, write_len, true);
        }
        if (read_len > 0 && read_data != NULL) {
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
            i2c_master_read(cmd, read_data, read_len, I2C_MASTER_LAST_NACK);
        }
        i2c_master_stop(cmd);
        err = i2c_master_cmd_begin(s_port, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        if (err == ESP_OK) {
            break;
        }
        ++s_error_count;
        ESP_LOGW(TAG, "I2C 0x%02X error: %s", addr, esp_err_to_name(err));
        i2c_bus_recover();
    }
    return err;
}

uint32_t i2c_bus_get_error_count(void) {
    return s_error_count;
}

