idf_component_register(SRCS "ota_update.c"
                      INCLUDE_DIRS "."
                      REQUIRES esp_https_ota esp_system)
