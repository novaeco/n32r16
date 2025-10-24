cmake_minimum_required(VERSION 3.16.0)
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/../common/proto
    ${CMAKE_CURRENT_LIST_DIR}/../common/net
    ${CMAKE_CURRENT_LIST_DIR}/../common/util
    ${CMAKE_CURRENT_LIST_DIR}/../components/esp_lcd_panel_rgb)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hmi_node)
