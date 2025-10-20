#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint8_t port_a;
    uint8_t port_b;
} mcp23017_state_t;

esp_err_t mcp23017_init(uint8_t address, uint16_t dir_mask);
esp_err_t mcp23017_write(uint8_t address, uint8_t port, uint8_t mask, uint8_t value);
esp_err_t mcp23017_read(uint8_t address, mcp23017_state_t *state);

