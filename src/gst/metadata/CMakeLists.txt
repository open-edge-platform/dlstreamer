# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "dlstreamer_gst_meta")

file(GLOB MAIN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
file(GLOB MAIN_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

add_library(${TARGET_NAME} SHARED ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME}
        PUBLIC
                ${GSTREAMER_INCLUDE_DIRS}
                ${GSTVIDEO_INCLUDE_DIRS}
                ${GLIB2_INCLUDE_DIRS}
        PRIVATE
                ${GSTALLOC_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
        PUBLIC
                dlstreamer_api
        PRIVATE
                ${GSTREAMER_LIBRARIES}
                ${GSTVIDEO_LIBRARIES}
                ${GLIB2_LIBRARIES}
                ${GSTALLOC_LIBRARIES}
                ${GSTANALYTICS_LIBRARIES}
)

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
