# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "gvametapublish")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)


file (GLOB MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/base/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/file/*.cpp
)
file (GLOB MAIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/base/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/file/*.hpp
)

add_library(${TARGET_NAME} SHARED ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

include(GenerateExportHeader)
generate_export_header(${TARGET_NAME}
    BASE_NAME ${TARGET_NAME}
    EXPORT_MACRO_NAME GVAMETAPUBLISH_EXPORTS
)

target_include_directories(${TARGET_NAME}
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/base
    ${CMAKE_CURRENT_BINARY_DIR}
PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
PRIVATE
    gstvideoanalyticsmeta
    utils
    ${GSTREAMER_LIBRARIES}
)

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})

if (${ENABLE_PAHO_INSTALLATION})
    add_subdirectory(mqtt)
endif()
if(${ENABLE_RDKAFKA_INSTALLATION})
    add_subdirectory(kafka)
endif()
