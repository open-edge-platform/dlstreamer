# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "test_metapublish_kafka")

find_package(PkgConfig REQUIRED)
if(WIN32)
    find_package(RdKafka CONFIG REQUIRED)
    set(KAFKA RdKafka::rdkafka++)
else()
    find_library(KAFKA rdkafka++ REQUIRED)
endif()
pkg_check_modules(GSTBASE gstreamer-base-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)

file (GLOB MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file (GLOB MAIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_executable(${TARGET_NAME} ${MAIN_SRC} ${MAIN_HEADERS})
target_include_directories(${TARGET_NAME}
PRIVATE
    ${GLIB2_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
)
target_link_libraries(${TARGET_NAME}
PRIVATE
    gtest
    gmock
    ${KAFKA}
    gvametapublishkafka
    ${GSTREAMER_LIBRARIES}
    ${GSTBASE_LIBRARIES}
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
