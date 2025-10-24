idf_component_register(SRCS "wifi_sta.c" "mdns_support.c" "ws_server_core.c" "ws_client_core.c"
                      "wifi_provisioner.c" "tls_credentials.c"
                      INCLUDE_DIRS "include"
                      REQUIRES esp_wifi esp_event esp_timer esp_http_server esp_netif esp_https_server wifi_provisioning esp_secure_cert_mgr
                      PRIV_REQUIRES nvs_flash)
