idf_component_register(SRCS "wifi_sta.c" "mdns_support.c" "ws_server_core.c" "ws_client_core.c"
                      INCLUDE_DIRS "include"
                      REQUIRES esp_netif esp_wifi esp_event esp_http_server esp_http_client mdns cjson common_util)
