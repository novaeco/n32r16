idf_component_register(SRCS "ringbuf.c" "time_sync.c" "monotonic.c"
                      INCLUDE_DIRS "."
                      REQUIRES esp_timer esp_sntp)
