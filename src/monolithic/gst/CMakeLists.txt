# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

if(UNIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
endif()

add_subdirectory(runtime_feature_toggling)
add_subdirectory(common)
add_subdirectory(elements)
add_subdirectory(3d_elements)
add_subdirectory(inference_elements)
add_subdirectory(registrator)
add_subdirectory(3d_registrator)

if(${ENABLE_AUDIO_INFERENCE_ELEMENTS})
    add_subdirectory(audio_inference_elements)
endif()

# [INTERNAL-ANCHOR-BEGIN]
if(${ENABLE_MTCNN})
    add_subdirectory(gvamtcnn)
endif()
# [INTERNAL-ANCHOR-END]
