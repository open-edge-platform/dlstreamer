# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "model_proc")
set (CMAKE_CXX_STANDARD 20)

file(GLOB MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB MAIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_library(${TARGET_NAME} STATIC ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME}
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GSTREAMER_INCLUDE_DIRS}
    ${GLIB2_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
PUBLIC
    ${GSTREAMER_LIBRARIES}
    ${GLIB2_LIBRARIES}
    json-hpp
    json-schema-validator
)
