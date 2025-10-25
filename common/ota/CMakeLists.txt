idf_component_register(SRCS "ota_update.c"
                      INCLUDE_DIRS "."
                      REQUIRES esp_https_ota esp_system)
if(CONFIG_COMMON_ENABLE_GCOV)
    target_compile_options(${COMPONENT_LIB} PRIVATE -fprofile-arcs -ftest-coverage)
    target_link_options(${COMPONENT_LIB} PRIVATE -fprofile-arcs -ftest-coverage)
endif()
