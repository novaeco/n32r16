#include "mcp23017.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

#define MCP23017_IODIRA 0x00
#define MCP23017_IODIRB 0x01
#define MCP23017_GPPUA 0x0C
#define MCP23017_GPPUB 0x0D
#define MCP23017_GPIOA 0x12
#define MCP23017_GPIOB 0x13
#define MCP23017_OLATA 0x14
#define MCP23017_OLATB 0x15

/**
 * @brief Write a 16-bit value to consecutive MCP23017 registers.
 *
 * @param addr I2C slave address.
 * @param reg Starting register address.
 * @param value Word value to write (low byte first).
 * @return ESP_OK when the transfer succeeds, otherwise an error code.
 */
static esp_err_t mcp23017_write16(uint8_t addr, uint8_t reg, uint16_t value)
{
    uint8_t data[3] = {reg, (uint8_t)(value & 0xFF), (uint8_t)(value >> 8)};
    return i2c_bus_write(addr, data, sizeof(data));
}

/**
 * @brief Read a 16-bit value from the MCP23017 register pair.
 *
 * @param addr I2C slave address.
 * @param reg Starting register address.
 * @param value Output pointer receiving the register contents.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
static esp_err_t mcp23017_read16(uint8_t addr, uint8_t reg, uint16_t *value)
{
    uint8_t reg_addr = reg;
    uint8_t buf[2] = {0};
    ESP_RETURN_ON_ERROR(i2c_bus_write_read(addr, &reg_addr, 1, buf, sizeof(buf)), "mcp23017", "read");
    *value = ((uint16_t)buf[1] << 8) | buf[0];
    return ESP_OK;
}

/**
 * @brief Initialize MCP23017 direction and pull-up configuration.
 *
 * @param addr I2C slave address.
 * @param direction_mask Bit mask selecting input (1) or output (0) pins.
 * @param pullup_mask Pull-up enable mask for input pins.
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t mcp23017_init(uint8_t addr, uint16_t direction_mask, uint16_t pullup_mask)
{
    ESP_RETURN_ON_ERROR(mcp23017_write16(addr, MCP23017_IODIRA, direction_mask), "mcp23017", "iodir");
    ESP_RETURN_ON_ERROR(mcp23017_write16(addr, MCP23017_GPPUA, pullup_mask), "mcp23017", "gppu");
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

/**
 * @brief Read GPIO input states from both MCP23017 ports.
 *
 * @param addr I2C slave address.
 * @param value Output pointer receiving the concatenated port value.
 * @return ESP_OK when the read succeeds, otherwise an error code.
 */
esp_err_t mcp23017_read_gpio(uint8_t addr, uint16_t *value)
{
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    return mcp23017_read16(addr, MCP23017_GPIOA, value);
}

/**
 * @brief Update GPIO output latches on the MCP23017 using a mask.
 *
 * @param addr I2C slave address.
 * @param mask Bit mask indicating which pins to update.
 * @param value Output value applied to masked pins.
 * @return ESP_OK on success or an ESP-IDF error code.
 */
esp_err_t mcp23017_write_gpio(uint8_t addr, uint16_t mask, uint16_t value)
{
    uint16_t current;
    ESP_RETURN_ON_ERROR(mcp23017_read16(addr, MCP23017_OLATA, &current), "mcp23017", "olat");
    current = (current & ~mask) | (value & mask);
    return mcp23017_write16(addr, MCP23017_OLATA, current);
}
