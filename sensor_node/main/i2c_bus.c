#include "i2c_bus.h"

#include "board_pins.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "io/io_map.h"
#include "esp_rom/ets_sys.h"

#define I2C_MASTER_PORT I2C_NUM_0
#define I2C_CMD_TIMEOUT_MS 100
#define I2C_RECOVER_THRESHOLD 3
#define I2C_BACKOFF_BASE_MS 5
#define I2C_SCAN_ADDR_MIN 0x08
#define I2C_SCAN_ADDR_MAX 0x77

static const char *TAG = "i2c_bus";

static i2c_config_t s_i2c_config;
static bool s_initialized;
static SemaphoreHandle_t s_mutex;
static StaticSemaphore_t s_mutex_buffer;
static uint8_t s_consecutive_failures;

static void i2c_bus_lock(void)
{
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void i2c_bus_unlock(void)
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

static esp_err_t i2c_bus_apply_config(void)
{
    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_MASTER_PORT, &s_i2c_config), TAG, "param config");
    return i2c_driver_install(I2C_MASTER_PORT, s_i2c_config.mode, 0, 0, 0);
}

static void i2c_bus_force_idle(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << SENSOR_NODE_I2C_SDA) | (1ULL << SENSOR_NODE_I2C_SCL),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(SENSOR_NODE_I2C_SDA, 1);
    for (int i = 0; i < 16; ++i) {
        gpio_set_level(SENSOR_NODE_I2C_SCL, 0);
        ets_delay_us(5);
        gpio_set_level(SENSOR_NODE_I2C_SCL, 1);
        ets_delay_us(5);
    }
    ets_delay_us(5);
    gpio_reset_pin(SENSOR_NODE_I2C_SDA);
    gpio_reset_pin(SENSOR_NODE_I2C_SCL);
}

static esp_err_t i2c_bus_recover_locked(void)
{
    ESP_LOGW(TAG, "Re-initialising I2C bus after persistent failures");
    i2c_driver_delete(I2C_MASTER_PORT);
    i2c_bus_force_idle();
    return i2c_bus_apply_config();
}

static esp_err_t i2c_bus_cmd_begin(i2c_cmd_handle_t cmd)
{
    esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_PORT, cmd, pdMS_TO_TICKS(I2C_CMD_TIMEOUT_MS));
    if (err == ESP_OK) {
        s_consecutive_failures = 0;
        return ESP_OK;
    }
    ++s_consecutive_failures;
    ESP_LOGW(TAG, "I2C transaction failed (%s), failure count=%u", esp_err_to_name(err),
             (unsigned)s_consecutive_failures);
    if (s_consecutive_failures >= I2C_RECOVER_THRESHOLD) {
        if (i2c_bus_recover_locked() == ESP_OK) {
            s_consecutive_failures = 0;
        }
    }
    return err;
}

static esp_err_t i2c_master_cmd(uint8_t addr, const uint8_t *wr_data, size_t wr_len, uint8_t *rd_data,
                                size_t rd_len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_FAIL;
    i2c_bus_lock();
    for (int attempt = 0; attempt < 3; ++attempt) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        if (!cmd) {
            i2c_bus_unlock();
            return ESP_ERR_NO_MEM;
        }
        esp_err_t cmd_err = i2c_master_start(cmd);
        if (cmd_err == ESP_OK && wr_len > 0) {
            cmd_err = i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
            if (cmd_err == ESP_OK && wr_data && wr_len) {
                cmd_err = i2c_master_write(cmd, (uint8_t *)wr_data, wr_len, true);
            }
        }
        if (cmd_err == ESP_OK && rd_len > 0) {
            if (wr_len > 0) {
                cmd_err = i2c_master_start(cmd);
            }
            if (cmd_err == ESP_OK) {
                cmd_err = i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
            }
            if (cmd_err == ESP_OK) {
                if (rd_len > 1) {
                    cmd_err = i2c_master_read(cmd, rd_data, rd_len - 1, I2C_MASTER_ACK);
                }
                if (cmd_err == ESP_OK) {
                    cmd_err = i2c_master_read_byte(cmd, rd_data + rd_len - 1, I2C_MASTER_NACK);
                }
            }
        }
        if (cmd_err == ESP_OK) {
            cmd_err = i2c_master_stop(cmd);
        }
        if (cmd_err == ESP_OK) {
            err = i2c_bus_cmd_begin(cmd);
        } else {
            err = cmd_err;
        }
        i2c_cmd_link_delete(cmd);
        if (err == ESP_OK) {
            break;
        }
        uint32_t backoff = I2C_BACKOFF_BASE_MS << attempt;
        vTaskDelay(pdMS_TO_TICKS(backoff));
    }
    i2c_bus_unlock();
    return err;
}

static esp_err_t i2c_bus_probe(uint8_t addr, bool *present)
{
    if (present) {
        *present = false;
    }
    if (addr < I2C_SCAN_ADDR_MIN || addr > I2C_SCAN_ADDR_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ESP_FAIL;
    i2c_bus_lock();
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        i2c_bus_unlock();
        return ESP_ERR_NO_MEM;
    }
    esp_err_t cmd_err = i2c_master_start(cmd);
    if (cmd_err == ESP_OK) {
        cmd_err = i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    }
    if (cmd_err == ESP_OK) {
        cmd_err = i2c_master_stop(cmd);
    }
    if (cmd_err == ESP_OK) {
        err = i2c_bus_cmd_begin(cmd);
    } else {
        err = cmd_err;
    }
    i2c_cmd_link_delete(cmd);
    i2c_bus_unlock();
    if (err == ESP_OK && present) {
        *present = true;
    }
    return err;
}

static void i2c_bus_scan(void)
{
    const io_map_t *map = io_map_get();
    bool expected[128] = {0};
    uint8_t logical_instances[128] = {0};
    for (size_t i = 0; i < map->ambient_count && i < IO_MAX_AMBIENT_SENSORS; ++i) {
        uint8_t saddr = map->ambient[i].address & 0x7F;
        if (saddr) {
            expected[saddr] = true;
            logical_instances[saddr]++;
        }
    }
    for (size_t i = 0; i < 2; ++i) {
        uint8_t mcp = map->mcp23017_addresses[i] & 0x7F;
        expected[mcp] = true;
    }
    if (map->pwm_backend == IO_PWM_BACKEND_PCA9685 && map->pca9685_address) {
        expected[map->pca9685_address & 0x7F] = true;
    }

    uint8_t seen[128] = {0};
    for (uint8_t addr = I2C_SCAN_ADDR_MIN; addr <= I2C_SCAN_ADDR_MAX; ++addr) {
        bool present = false;
        if (i2c_bus_probe(addr, &present) == ESP_OK && present) {
            seen[addr]++;
            ESP_LOGI(TAG, "Detected device at 0x%02X", addr);
        }
    }

    for (uint8_t addr = 0; addr < 128; ++addr) {
        if (expected[addr] && seen[addr] == 0) {
            ESP_LOGE(TAG, "Expected device at 0x%02X not detected", addr);
        }
        if (seen[addr] > 1) {
            ESP_LOGW(TAG, "Address collision detected at 0x%02X (%u responders)", addr, (unsigned)seen[addr]);
        }
        if (!expected[addr] && seen[addr] > 0) {
            ESP_LOGW(TAG, "Unexpected device present at 0x%02X", addr);
        }
        if (logical_instances[addr] > 1) {
            ESP_LOGW(TAG, "Multiple logical peripherals mapped to 0x%02X; ensure external gating is configured",
                     addr);
        }
    }
}

esp_err_t i2c_bus_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    s_i2c_config.mode = I2C_MODE_MASTER;
    s_i2c_config.sda_io_num = SENSOR_NODE_I2C_SDA;
    s_i2c_config.scl_io_num = SENSOR_NODE_I2C_SCL;
    s_i2c_config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    s_i2c_config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    s_i2c_config.master.clk_speed = 400000;
    s_i2c_config.clk_flags = 0;

    ESP_RETURN_ON_ERROR(i2c_bus_apply_config(), TAG, "driver install");
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buffer);
        if (!s_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    s_consecutive_failures = 0;
    s_initialized = true;
    i2c_bus_scan();
    return ESP_OK;
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
