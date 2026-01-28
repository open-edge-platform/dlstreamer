# ==============================================================================
# Copyright (C) 2021-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

add_subdirectory(base)
add_subdirectory(cpu)
#add_subdirectory(ffmpeg)
add_subdirectory(gst)
add_subdirectory(utils)

if(NOT WIN32)
    add_subdirectory(opencl)
    add_subdirectory(opencv)
    add_subdirectory(openvino)
    add_subdirectory(sycl)
    add_subdirectory(vaapi)
endif()