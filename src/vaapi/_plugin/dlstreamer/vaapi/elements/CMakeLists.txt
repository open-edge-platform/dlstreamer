# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "dlstreamer_vaapi")

foreach(TYPE SHARED STATIC)
    if(${TYPE} STREQUAL "SHARED")
        set(LIB_NAME ${TARGET_NAME})
    else()
        set(LIB_NAME ${TARGET_NAME}_static)
    endif()
    add_library(${LIB_NAME} ${TYPE} elements.cpp)
    set_compile_flags(${LIB_NAME})

    target_include_directories(${LIB_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
    target_link_libraries(${LIB_NAME} PUBLIC dlstreamer_api vaapi_sync vaapi_batch_proc)
    set_target_properties(${LIB_NAME} PROPERTIES OUTPUT_NAME ${TARGET_NAME})
    install(TARGETS ${LIB_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
endforeach()