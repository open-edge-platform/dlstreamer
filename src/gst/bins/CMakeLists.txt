# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "gstdlstreamer_bins")

add_subdirectory(model_proc)
add_subdirectory(processbin)
add_subdirectory(video_inference)
add_subdirectory(object_detect)
add_subdirectory(object_classify)
add_subdirectory(object_track)
add_subdirectory(meta_overlay)

add_library(${TARGET_NAME} SHARED register_elements.cpp)
set_compile_flags(${TARGET_NAME})
if(WIN32)
    add_vs_version_resource(${TARGET_NAME})
endif()

target_link_libraries(${TARGET_NAME}
PUBLIC
        dlstreamer_gst
        processbin
        video_inference
        object_detect
        object_classify
        object_tracking
        meta_overlay
        )

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
