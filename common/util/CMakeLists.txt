idf_component_register(SRCS "ringbuf.c" "time_sync.c" "monotonic.c" "base64_utils.c" "base32_utils.c" "totp.c" "memory_profile.c"
                      INCLUDE_DIRS "."
                      REQUIRES esp_timer esp_sntp)

if(CONFIG_COMMON_ENABLE_GCOV)
    target_compile_options(${COMPONENT_LIB} PRIVATE -fprofile-arcs -ftest-coverage)
    target_link_options(${COMPONENT_LIB} PRIVATE -fprofile-arcs -ftest-coverage)
endif()
