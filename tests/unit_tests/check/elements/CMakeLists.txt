# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

add_subdirectory(test_common)
add_subdirectory(inference)
add_subdirectory(classification)
add_subdirectory(metaconvert)
add_subdirectory(metapublish)
add_subdirectory(fpscounter)
add_subdirectory(watermark)
add_subdirectory(properties)

if(${ENABLE_AUDIO_INFERENCE_ELEMENTS})
    add_subdirectory(audiodetect)
endif()

# [INTERNAL-ANCHOR-BEGIN]
if(${ENABLE_MTCNN})
    add_subdirectory(bboxregression)
    add_subdirectory(nms)
endif()
# [INTERNAL-ANCHOR-END]
