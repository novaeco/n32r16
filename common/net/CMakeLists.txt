idf_component_register(SRCS "wifi_manager.c" "mdns_helper.c" "ws_server.c" "ws_client.c"
                      INCLUDE_DIRS "."
                      REQUIRES esp_wifi esp_http_server mdns esp_websocket_client)
