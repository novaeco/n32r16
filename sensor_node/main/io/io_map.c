#include "io_map.h"

static const io_mapping_t s_mappings[] = {
    {IO_BANK_MCP0, 0, 0x01},
    {IO_BANK_MCP0, 0, 0x02},
    {IO_BANK_MCP0, 0, 0x04},
    {IO_BANK_MCP0, 0, 0x08},
    {IO_BANK_MCP0, 1, 0x01},
    {IO_BANK_MCP0, 1, 0x02},
    {IO_BANK_MCP1, 0, 0x01},
    {IO_BANK_MCP1, 0, 0x02},
    {IO_BANK_MCP1, 0, 0x04},
    {IO_BANK_MCP1, 1, 0x01},
};

const io_mapping_t *io_map_get(void) {
    return s_mappings;
}

size_t io_map_count(void) {
    return sizeof(s_mappings) / sizeof(s_mappings[0]);
}

