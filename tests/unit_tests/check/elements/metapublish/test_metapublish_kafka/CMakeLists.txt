# ==============================================================================
# Copyright (C) 2018-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "test_metapublish_kafka")

include_directories("${CMAKE_BINARY_DIR}/deps/install/include;${CMAKE_BINARY_DIR}/deps/gstreamer-bin/include;${CMAKE_BINARY_DIR}/deps/opencv-bin/include;${CMAKE_BINARY_DIR}/deps/rdkafka-bin/include")


find_package(PkgConfig REQUIRED)
find_package(RdKafka 2.12.1...<2.14 REQUIRED)
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
    RdKafka::rdkafka++
    gvametapublishkafka
    ${GSTREAMER_LIBRARIES}
    ${GSTBASE_LIBRARIES}
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
