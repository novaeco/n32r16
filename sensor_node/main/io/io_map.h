#pragma once

#include <stdint.h>

typedef struct {
    uint8_t sht20_addresses[2];
    uint8_t pca9685_address;
    uint8_t mcp23017_addresses[2];
} io_map_t;

const io_map_t *io_map_get(void);
