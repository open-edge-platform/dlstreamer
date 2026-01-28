# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "dlstreamer_gst_meta")

file(GLOB MAIN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
file(GLOB MAIN_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
set(SHARED_LIB ${TARGET_NAME})
set(STATIC_LIB ${TARGET_NAME}_static)

foreach(LIB_TYPE IN ITEMS SHARED STATIC)
        if(LIB_TYPE STREQUAL "SHARED")
                set(LIB_NAME ${SHARED_LIB})
        else()
                set(LIB_NAME ${STATIC_LIB})
        endif()

        add_library(${LIB_NAME} ${LIB_TYPE} ${MAIN_SRC} ${MAIN_HEADERS})
        set_compile_flags(${LIB_NAME})

        target_include_directories(${LIB_NAME}
                PUBLIC
                        ${GSTREAMER_INCLUDE_DIRS}
                        ${GSTVIDEO_INCLUDE_DIRS}
                        ${GLIB2_INCLUDE_DIRS}
                PRIVATE
                        ${GSTALLOC_INCLUDE_DIRS}
        )

        target_link_libraries(${LIB_NAME}
                PUBLIC
                        dlstreamer_api
                PRIVATE
                        ${GSTREAMER_LIBRARIES}
                        ${GSTVIDEO_LIBRARIES}
                        ${GLIB2_LIBRARIES}
                        ${GSTALLOC_LIBRARIES}
                        ${GSTANALYTICS_LIBRARIES}
        )

        set_target_properties(${LIB_NAME} PROPERTIES OUTPUT_NAME ${TARGET_NAME})
        install(TARGETS ${LIB_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
endforeach()

# Generate GObject Introspection (GIR) files
include(generate_gir.cmake)