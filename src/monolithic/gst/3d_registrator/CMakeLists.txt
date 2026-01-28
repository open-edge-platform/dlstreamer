# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "gst3delements")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)

add_library(${TARGET_NAME} SHARED register_3d_elements.cpp)
set_compile_flags(${TARGET_NAME})

if(UNIX)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=unused-variable -Wno-error=unused-parameter)
endif()

target_include_directories(${TARGET_NAME}
PUBLIC
    ${DLSTREAMER_BASE_DIR}/include/dlstreamer/gst/metadata
)

target_link_libraries(${TARGET_NAME}
PRIVATE
    ${GSTREAMER_LIBRARIES}
    dlstreamer_gst_meta
)

if(NOT WIN32 AND TARGET g3dlidarparse)
    target_link_libraries(${TARGET_NAME} PRIVATE g3dlidarparse)
    target_compile_definitions(${TARGET_NAME} PRIVATE HAVE_G3DLIDARPARSE)
endif()

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
