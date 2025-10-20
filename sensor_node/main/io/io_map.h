#pragma once

#include <stdint.h>

typedef enum {
    IO_BANK_MCP0,
    IO_BANK_MCP1,
} io_bank_t;

typedef struct {
    io_bank_t bank;
    uint8_t port;
    uint8_t mask;
} io_mapping_t;

const io_mapping_t *io_map_get(void);
size_t io_map_count(void);

