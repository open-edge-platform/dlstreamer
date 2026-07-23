# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# This element has NO compile-time dependency on any vendor SDK. It talks to a
# vendor backend (libg3dlidar_<vendor>.so, see backends/) only at runtime, which
# it dlopen()s in start() by a name derived from the config `vendor` field. Each
# vendor's SDK, FetchContent/patches and build quirks live in that backend's
# CMakeLists.txt, not here.

set(TARGET_NAME "g3dlidarsrc")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)

add_library(${TARGET_NAME} STATIC
    g3dlidarsrc.cpp
)
set_compile_flags(${TARGET_NAME})

if(UNIX)
    target_compile_options(${TARGET_NAME} PRIVATE
        -Wno-error=unused-variable
        -Wno-error=unused-parameter
    )
endif()

if(WIN32)
    if(COMMAND add_vs_version_resource)
        add_vs_version_resource(${TARGET_NAME})
    endif()
endif()

target_include_directories(${TARGET_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include  # dlstreamer/lidar/g3d_lidar_backend_api.h
        ${DLSTREAMER_BASE_DIR}/include/dlstreamer/gst/metadata
    PRIVATE
        ${GSTREAMER_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
    PRIVATE
        json-hpp  # nlohmann/json for parsing the LiDAR config file
        ${CMAKE_DL_LIBS}  # dlopen/dlsym for loading the vendor backend
)

# Windows: dlfcn-win32 provides the dlopen/dlsym implementation.
if(WIN32)
    find_package(dlfcn-win32 REQUIRED)
    target_include_directories(${TARGET_NAME} PRIVATE ${dlfcn-win32_INCLUDE_DIRS})
    target_link_libraries(${TARGET_NAME} PRIVATE ${dlfcn-win32_LIBRARIES})
endif()

# Vendor backends (libg3dlidar_<vendor>.so). Each is opt-in and loaded at
# runtime by the element via dlopen; none are linked here. See backends/.
add_subdirectory(backends)
