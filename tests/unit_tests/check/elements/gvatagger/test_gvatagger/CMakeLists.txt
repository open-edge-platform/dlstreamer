# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "test_gvatagger")

file(GLOB MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(${TARGET_NAME} ${MAIN_SRC})

target_include_directories(${TARGET_NAME}
PRIVATE
    ${DLSTREAMER_BASE_DIR}/src/monolithic/gst/elements/gvatagger
)
# codecparsers headers #warning without it, which -Werror turns into a build failure.
target_compile_definitions(${TARGET_NAME} PRIVATE GST_USE_UNSTABLE_API)

target_link_libraries(${TARGET_NAME}
PRIVATE
    test_common
    test_utils
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
