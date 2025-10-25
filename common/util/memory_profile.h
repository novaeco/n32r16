#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t chip_model;
    uint32_t chip_revision;
    uint32_t cores;
    size_t flash_size_bytes;
    size_t psram_size_bytes;
    size_t internal_ram_total_bytes;
    size_t internal_ram_free_bytes;
    size_t psram_free_bytes;
    bool psram_available;
} memory_profile_t;

esp_err_t memory_profile_init(void);
const memory_profile_t *memory_profile_get(void);
void memory_profile_log(void);
size_t memory_profile_recommend_draw_buffer_px(uint32_t hor_res, uint32_t ver_res, uint32_t min_lines);

