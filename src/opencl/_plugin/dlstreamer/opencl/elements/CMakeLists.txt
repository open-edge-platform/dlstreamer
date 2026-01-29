# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "dlstreamer_opencl")

if(VA_FOUND)
    list(APPEND LINK_LIBS vaapi_to_opencl)
endif()

add_library(${TARGET_NAME} SHARED elements.cpp)
set_compile_flags(${TARGET_NAME})
target_link_libraries(${TARGET_NAME} PUBLIC opencl_tensor_normalize)
install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
