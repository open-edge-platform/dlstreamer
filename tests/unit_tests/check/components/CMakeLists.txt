# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

add_subdirectory(classification_history)
add_subdirectory(gstvideoanalyticsmeta)
add_subdirectory(metaaggregate_copy)
add_subdirectory(safe_arithmetic)
add_subdirectory(feature_toggler)
add_subdirectory(feature_reader)
add_subdirectory(oo-permissions)
add_subdirectory(postprocessing)
add_subdirectory(null-byte-injection)
add_subdirectory(regular-expression)
add_subdirectory(so_loader)
add_subdirectory(symlink)
add_subdirectory(preprocessing)
add_subdirectory(utils)


if(${ENABLE_AUDIO_INFERENCE_ELEMENTS})
    add_subdirectory(audio)
endif()

if(${ENABLE_VAAPI})
    add_subdirectory(va-api-pre-proc)
endif()
