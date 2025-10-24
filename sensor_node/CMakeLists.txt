cmake_minimum_required(VERSION 3.23)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/../components"
    "${CMAKE_SOURCE_DIR}/../common/util"
    "${CMAKE_SOURCE_DIR}/../common/net"
    "${CMAKE_SOURCE_DIR}/../common/proto"
    "${CMAKE_SOURCE_DIR}/../common/security"
    "${CMAKE_SOURCE_DIR}/../tests"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sensor_node)
