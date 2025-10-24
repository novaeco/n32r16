idf_component_register(SRCS "command_auth.c"
                      INCLUDE_DIRS "include"
                      REQUIRES mbedtls cjson common_proto
                      PRIV_REQUIRES nvs_flash)
