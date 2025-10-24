set(CERT_FILES
    "${CMAKE_CURRENT_LIST_DIR}/certs/server_cert.pem"
    "${CMAKE_CURRENT_LIST_DIR}/certs/server_key.pem"
    "${CMAKE_CURRENT_LIST_DIR}/certs/ca_cert.pem"
)

idf_component_register(SRCS "cert_store.c"
                      INCLUDE_DIRS "include")

idf_component_add_resources(SRCS ${CERT_FILES})
