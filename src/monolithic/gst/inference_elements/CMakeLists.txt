# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

add_subdirectory(vision_inference)

if(${ENABLE_AUDIO_INFERENCE_ELEMENTS})
    add_subdirectory(audio_inference)
endif()

if(ENABLE_GENAI)
    add_subdirectory(genai_inference)
endif()
