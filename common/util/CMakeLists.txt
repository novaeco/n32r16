idf_component_register(SRCS "ringbuf.c" "time_sync.c" "monotonic_clock.c"
                      INCLUDE_DIRS "include"
                      REQUIRES esp_timer freertos esp_sntp)
