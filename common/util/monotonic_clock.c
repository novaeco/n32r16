#include "monotonic_clock.h"

#include "esp_timer.h"

void monotonic_clock_mark(void) {
    // Function reserved for future time sync hooks. No-op.
}

uint32_t monotonic_clock_elapsed_ms(uint32_t since) {
    uint32_t now = monotonic_clock_now_ms();
    return now - since;
}

uint32_t monotonic_clock_now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

