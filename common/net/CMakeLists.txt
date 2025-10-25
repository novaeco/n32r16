idf_component_register(SRCS "wifi_manager.c" "mdns_helper.c" "ws_server.c" "ws_client.c" "ws_security.c"
                      INCLUDE_DIRS "."
                      PRIV_INCLUDE_DIRS "../util"
                      REQUIRES esp_wifi esp_http_server mdns esp_websocket_client mbedtls
                      TEST_SRCS "tests/test_mdns_helper.c" "tests/test_ws_client.c" "tests/test_ws_security.c" "tests/test_ws_server.c" "tests/test_wifi_manager.c"
                      TEST_INCLUDE_DIRS "tests")
if(CONFIG_COMMON_ENABLE_GCOV)
    target_compile_options(${COMPONENT_LIB} PRIVATE -fprofile-arcs -ftest-coverage)
    target_link_options(${COMPONENT_LIB} PRIVATE -fprofile-arcs -ftest-coverage)
endif()
