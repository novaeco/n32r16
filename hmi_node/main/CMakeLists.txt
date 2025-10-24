idf_component_register(SRCS
    "app_main.c"
    "data_model.c"
    "display/lvgl_port.c"
    "display/ui_screens.c"
    "net/ws_client.c"
    "tasks/t_ui.c"
    "tasks/t_net_rx.c"
    "tasks/t_heartbeat.c"
    "touch/gt911.c"
    INCLUDE_DIRS "." "display" "net" "tasks" "touch"
    REQUIRES common_proto common_net common_util lvgl esp_lcd_panel_rgb mdns)
