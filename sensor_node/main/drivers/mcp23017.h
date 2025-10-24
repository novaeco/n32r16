#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef enum {
    MCP23017_PORTA = 0,
    MCP23017_PORTB = 1,
} mcp23017_port_t;

esp_err_t mcp23017_init(uint8_t addr, uint16_t direction_mask, uint16_t pullup_mask);
esp_err_t mcp23017_read_gpio(uint8_t addr, uint16_t *value);
esp_err_t mcp23017_write_gpio(uint8_t addr, uint16_t mask, uint16_t value);
