# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "gstdlstreamer_cpu")

file(GLOB_RECURSE ELEMENTS_CPP_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../*/elements.cpp")

add_library(${TARGET_NAME} SHARED register_elements.cpp ${ELEMENTS_CPP_FILE})
set_compile_flags(${TARGET_NAME})
if(WIN32)
    add_vs_version_resource(${TARGET_NAME})
endif()

target_link_libraries(${TARGET_NAME}
PRIVATE
    dlstreamer_api
    dlstreamer_cpu
    dlstreamer_gst
    dlstreamer_logger
    rate_adjust
    meta_smooth
)
if(WIN32)
    target_link_libraries(${TARGET_NAME}
        PRIVATE
            tensor_convert
            tensor_histogram
            tensor_sliding_window
            tensor_postproc
    )
endif()

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
