#pragma once

#include <stdbool.h>
#include <stdint.h>

void time_sync_init(void);
uint64_t time_sync_get_monotonic_ms(void);
uint64_t time_sync_get_unix_ms(void);
bool time_sync_is_synchronized(void);

