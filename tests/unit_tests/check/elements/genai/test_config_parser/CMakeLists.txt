# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "test_gvagenai_config_parser")

file(GLOB MAIN_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        )

file(GLOB MAIN_HEADERS
        ${CMAKE_CURRENT_SOURCE_DIR}/*.h
        )

add_executable(${TARGET_NAME} ${MAIN_SRC} ${MAIN_HEADERS})

# gvagenai is a static library; expose its source dir so the test can include configs.hpp
target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/monolithic/gst/elements/gvagenai
)

target_link_libraries(${TARGET_NAME}
PRIVATE
        test_common
        gvagenai
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
