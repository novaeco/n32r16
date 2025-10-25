idf_component_register(SRCS "cert_store.c"
                      INCLUDE_DIRS "include"
                      PRIV_REQUIRES nvs_flash spiffs
                      EMBED_TXTFILES
                          "${CMAKE_CURRENT_LIST_DIR}/certs/server_cert.pem"
                          "${CMAKE_CURRENT_LIST_DIR}/certs/server_key.pem"
                          "${CMAKE_CURRENT_LIST_DIR}/certs/ca_cert.pem")
