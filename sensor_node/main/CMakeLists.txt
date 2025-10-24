set(CERT_DIR "${PROJECT_SOURCE_DIR}/common/net/certs")

idf_component_register(SRCS "app_main.c" "data_model.c" "i2c_bus.c" "onewire_bus.c"
                      "net/ws_server.c" "security/command_auth.c"
                      "drivers/sht20.c" "drivers/pca9685.c" "drivers/mcp23017.c"
                      "drivers/ds18b20.c" "io/io_map.c"
                      "tasks/t_sensors.c" "tasks/t_io.c" "tasks/t_heartbeat.c"
                      INCLUDE_DIRS "." "drivers" "io" "tasks" "net" "security"
                      REQUIRES common_util common_net common_proto cjson common_security)
