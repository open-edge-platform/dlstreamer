# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "dlstreamer_gst")

if(WIN32)
    pkg_check_modules(GSTD3D11 gstreamer-d3d11-1.0>=1.16 REQUIRED)
endif()

add_library(${TARGET_NAME} SHARED allocator.cpp transform_element.cpp)
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME}
PUBLIC
        ${GSTREAMER_INCLUDE_DIRS}
        ${GSTVIDEO_INCLUDE_DIRS}
        ${GLIB2_INCLUDE_DIRS}
PRIVATE
        ${GSTALLOC_INCLUDE_DIRS}
        )

if(WIN32)
    target_include_directories(${TARGET_NAME}
    PRIVATE
        ${GSTD3D11_INCLUDE_DIRS}
    )
endif()

target_link_libraries(${TARGET_NAME}
PUBLIC
        dlstreamer_api
        dlstreamer_gst_utils
        dlstreamer_gst_meta
        gst_logger_sink
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
        ${GLIB2_LIBRARIES}
PRIVATE
        ${GSTALLOC_LIBRARIES}
)

if(WIN32)
    target_link_libraries(${TARGET_NAME}
    PRIVATE
        ${GSTD3D11_LIBRARIES}
    )
endif()

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})

execute_process(COMMAND which icpx RESULT_VARIABLE status OUTPUT_VARIABLE out OUTPUT_QUIET )
if(status EQUAL 0)
    add_subdirectory(sycl)
else()
    message("Intel® oneAPI DPC++ Compiler not found, skipping dlstreamer_gst_sycl library")
endif()
