# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "dlstreamer_openvino")

find_package(OpenVINO REQUIRED)

set(SOURCES elements.cpp)
set(LINK_LIBS openvino::runtime logger dlstreamer_api openvino_inference)

foreach(TYPE SHARED STATIC)
    if(${TYPE} STREQUAL "SHARED")
        set(LIB_NAME ${TARGET_NAME})
    else()
        set(LIB_NAME ${TARGET_NAME}_static)
    endif()
    add_library(${LIB_NAME} ${TYPE} ${SOURCES})
    set_compile_flags(${LIB_NAME})
    target_link_libraries(${LIB_NAME} PRIVATE ${LINK_LIBS})
    set_target_properties(${LIB_NAME} PROPERTIES OUTPUT_NAME ${TARGET_NAME})
    install(TARGETS ${LIB_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
endforeach()