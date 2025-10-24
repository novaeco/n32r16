#include "monotonic.h"

#include "esp_timer.h"

uint64_t monotonic_time_us(void)
{
    return esp_timer_get_time();
}

uint32_t monotonic_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}
