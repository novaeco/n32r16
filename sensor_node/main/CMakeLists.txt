idf_component_register(SRCS
    "app_main.c"
    "data_model.c"
    "i2c_bus.c"
    "onewire_bus_manager.c"
    "net/ws_server.c"
    "io/io_map.c"
    "drivers/sht20.c"
    "drivers/bme280.c"
    "drivers/pca9685.c"
    "drivers/mcp23017.c"
    "drivers/ds18b20.c"
    "tasks/t_sensors.c"
    "tasks/t_io.c"
    "tasks/t_heartbeat.c"
    INCLUDE_DIRS "." "io" "drivers" "tasks"
    REQUIRES common_util common_proto common_net common_ota cert_store onewire_bus driver
    TEST_SRCS
    "tests/i2c_mock.c"
    "tests/onewire_mock.c"
    "tests/test_sht20.c"
    "tests/test_ds18b20.c"
    "tests/test_pca9685.c"
    "tests/test_mcp23017.c"
    "tests/test_data_model.c"
    TEST_INCLUDE_DIRS "tests")

if(CONFIG_SENSOR_ENABLE_GCOV)
    target_compile_options(${COMPONENT_LIB} PRIVATE -fprofile-arcs -ftest-coverage)
    target_link_options(${COMPONENT_LIB} PRIVATE -fprofile-arcs -ftest-coverage)
endif()
