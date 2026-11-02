# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

add_subdirectory(vision)

if(${ENABLE_AUDIO_INFERENCE_ELEMENTS})
    add_subdirectory(audio)
endif()

if(ENABLE_GENAI)
    add_subdirectory(genai)
endif()
