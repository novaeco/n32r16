idf_component_register(SRCS
    "app_main.c"
    "data_model.c"
    "prefs_store.c"
    "display/lvgl_port.c"
    "display/ui_locale.c"
    "display/ui_screens.c"
    "net/ws_client.c"
    "tasks/t_ui.c"
    "tasks/t_net_rx.c"
    "tasks/t_heartbeat.c"
    "touch/gt911.c"
    INCLUDE_DIRS "." "display" "net" "tasks" "touch"
    REQUIRES common_proto common_net common_util common_ota cert_store lvgl esp_lcd_panel_rgb mdns
    TEST_SRCS "tests/test_ui_events.c" "tests/test_ui_locale.c"
    TEST_INCLUDE_DIRS "tests")
if(CONFIG_HMI_ENABLE_GCOV)
    target_compile_options(${COMPONENT_LIB} PRIVATE -fprofile-arcs -ftest-coverage)
    target_link_options(${COMPONENT_LIB} PRIVATE -fprofile-arcs -ftest-coverage)
endif()
