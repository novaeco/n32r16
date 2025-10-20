#pragma once

#include <stdint.h>

void monotonic_clock_mark(void);
uint32_t monotonic_clock_elapsed_ms(uint32_t since);
uint32_t monotonic_clock_now_ms(void);

