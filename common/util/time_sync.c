#include "time_sync.h"

#include "esp_timer.h"

void time_sync_init(void) {
    // esp_timer is initialized by IDF; no action required but kept for symmetry
}

uint64_t time_sync_get_monotonic_ms(void) {
    return esp_timer_get_time() / 1000ULL;
}

