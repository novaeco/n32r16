#include "io_map.h"

static const io_map_t io_map = {
    .sht20_addresses = {0x40, 0x40},
    .pca9685_address = 0x41,
    .mcp23017_addresses = {0x20, 0x21},
};

const io_map_t *io_map_get(void)
{
    return &io_map;
}
