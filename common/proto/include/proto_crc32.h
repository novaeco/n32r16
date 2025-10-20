#pragma once

#include <stddef.h>
#include <stdint.h>

uint32_t proto_crc32_compute(const uint8_t *data, size_t len);

