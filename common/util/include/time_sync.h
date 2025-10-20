#pragma once

#include <stdint.h>

void time_sync_init(void);
uint64_t time_sync_get_monotonic_ms(void);

