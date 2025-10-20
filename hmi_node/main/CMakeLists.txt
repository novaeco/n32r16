set(CERT_DIR "${PROJECT_SOURCE_DIR}/common/net/certs")

idf_component_register(SRCS "app_main.c" "data_model.c"
                      "display/lvgl_port.c" "display/ui_screens.c"
                      "touch/gt911.c"
                      "net/ws_client.c"
                      "tasks/t_ui.c" "tasks/t_net_rx.c" "tasks/t_hmi_heartbeat.c"
                      INCLUDE_DIRS "." "display" "touch" "net" "tasks"
                      REQUIRES common_util common_net common_proto cjson lvgl esp_lcd_panel_rgb
                      EMBED_TXTFILES "${CERT_DIR}/sensor_ws_server.crt")
