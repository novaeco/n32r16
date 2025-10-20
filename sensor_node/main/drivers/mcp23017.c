#include "mcp23017.h"

#include <string.h>

#include "esp_log.h"
#include "i2c_bus.h"

#define MCP_IODIRA 0x00
#define MCP_IODIRB 0x01
#define MCP_GPIOA 0x12
#define MCP_GPIOB 0x13
#define MCP_OLATA 0x14
#define MCP_OLATB 0x15

static const char *TAG = "mcp23017";

static esp_err_t mcp23017_write_reg(uint8_t address, uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_bus_write_read(I2C_NUM_0, address, data, sizeof(data), NULL, 0);
}

static esp_err_t mcp23017_read_reg(uint8_t address, uint8_t reg, uint8_t *value) {
    return i2c_bus_write_read(I2C_NUM_0, address, &reg, 1, value, 1);
}

esp_err_t mcp23017_init(uint8_t address, uint16_t dir_mask) {
    ESP_ERROR_CHECK(mcp23017_write_reg(address, MCP_IODIRA, dir_mask & 0xFF));
    ESP_ERROR_CHECK(mcp23017_write_reg(address, MCP_IODIRB, (dir_mask >> 8) & 0xFF));
    return ESP_OK;
}

esp_err_t mcp23017_write(uint8_t address, uint8_t port, uint8_t mask, uint8_t value) {
    uint8_t reg = port == 0 ? MCP_OLATA : MCP_OLATB;
    uint8_t current = 0;
    ESP_ERROR_CHECK(mcp23017_read_reg(address, reg, &current));
    current &= ~mask;
    current |= (value & mask);
    return mcp23017_write_reg(address, reg, current);
}

esp_err_t mcp23017_read(uint8_t address, mcp23017_state_t *state) {
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_ERROR_CHECK(mcp23017_read_reg(address, MCP_GPIOA, &state->port_a));
    ESP_ERROR_CHECK(mcp23017_read_reg(address, MCP_GPIOB, &state->port_b));
    return ESP_OK;
}

