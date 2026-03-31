# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "gvametapublishmqtt")

find_package(PkgConfig REQUIRED)
if(WIN32)
    find_package(eclipse-paho-mqtt-c CONFIG REQUIRED)
    set(MQTT eclipse-paho-mqtt-c::paho-mqtt3as)
    set(UUID rpcrt4)
else()
    find_library(MQTT paho-mqtt3as REQUIRED)
    find_library(UUID uuid REQUIRED)
endif()
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)

file (GLOB MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)
file (GLOB MAIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

add_library(${TARGET_NAME} SHARED ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})
if(WIN32)
    add_vs_version_resource(${TARGET_NAME})
endif()

target_include_directories(${TARGET_NAME}
PRIVATE
    ${GLIB2_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${TARGET_NAME}
PRIVATE
    ${GSTREAMER_LIBRARIES}
    ${MQTT}
    ${UUID}
    common
    gvametapublish
)

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
