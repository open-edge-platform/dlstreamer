# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# Livox LiDAR backend: wraps Livox-SDK2 in a shared library
# (libg3dlidar_livox.so) implementing the vendor-neutral C ABI.
# g3dlidarsrc dlopen()s it at runtime when the config vendor is "livox".
# It is NOT linked into the element or the plugin, so the Livox-SDK2
# dependency is fully contained here.

if(NOT UNIX)
    message(WARNING "g3dlidar_livox: only Linux is supported; skipping")
    return()
endif()

set(TARGET_NAME "g3dlidar_livox")

# Livox-SDK2 is a compiled library. Two ways to provide its sources:
#   A) User-supplied: set LIVOX_SDK2_SRC_DIR (CMake or environment variable)
#      to a Livox-SDK2 checkout (expected revision: LIVOX_SDK2_VERSION).
#   B) Default: leave it unset and the pinned revision is fetched.
# Either way only the SDK's static library is built (as PIC, no samples, no
# install step) and linked privately into the backend.
#
# Revision must be new enough to know the Mid-360S (device type 35).
set(LIVOX_SDK2_VERSION c0796f04c143143899c87a773d9f6b7136453c0b)

if(NOT LIVOX_SDK2_SRC_DIR AND DEFINED ENV{LIVOX_SDK2_SRC_DIR})
    set(LIVOX_SDK2_SRC_DIR "$ENV{LIVOX_SDK2_SRC_DIR}")
endif()

include(ExternalProject)

set(_LIVOX_SDK2_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/livox_sdk2-build")
set(_LIVOX_SDK2_STATIC_LIB "${_LIVOX_SDK2_BUILD_DIR}/sdk_core/liblivox_lidar_sdk_static.a")
set(_LIVOX_SDK2_CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    # Hide all SDK symbols (incl. its bundled spdlog/rapidjson) inside the backend.
    "-DCMAKE_CXX_FLAGS=-fvisibility=hidden -fvisibility-inlines-hidden"
    # Upstream declares cmake_minimum_required(VERSION 3.0).
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)
set(_LIVOX_SDK2_BUILD_CMD ${CMAKE_COMMAND} --build <BINARY_DIR> --target livox_lidar_sdk_static -j)

if(LIVOX_SDK2_SRC_DIR)
    message(STATUS "g3dlidar_livox: using user-supplied Livox-SDK2 at ${LIVOX_SDK2_SRC_DIR} "
                   "(expected revision: ${LIVOX_SDK2_VERSION})")
    ExternalProject_Add(livox_sdk2_ext
        SOURCE_DIR       "${LIVOX_SDK2_SRC_DIR}"
        BINARY_DIR       "${_LIVOX_SDK2_BUILD_DIR}"
        CMAKE_ARGS       ${_LIVOX_SDK2_CMAKE_ARGS}
        BUILD_COMMAND    ${_LIVOX_SDK2_BUILD_CMD}
        INSTALL_COMMAND  ""
        BUILD_BYPRODUCTS "${_LIVOX_SDK2_STATIC_LIB}"
    )
    set(_LIVOX_SDK2_INCLUDE_DIR "${LIVOX_SDK2_SRC_DIR}/include")
else()
    message(STATUS "g3dlidar_livox: LIVOX_SDK2_SRC_DIR not set; fetching Livox-SDK2 ${LIVOX_SDK2_VERSION}")
    set(_LIVOX_SDK2_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/livox_sdk2-src")
    ExternalProject_Add(livox_sdk2_ext
        GIT_REPOSITORY   https://github.com/Livox-SDK/Livox-SDK2.git
        GIT_TAG          ${LIVOX_SDK2_VERSION}
        GIT_SHALLOW      FALSE
        SOURCE_DIR       "${_LIVOX_SDK2_SOURCE_DIR}"
        BINARY_DIR       "${_LIVOX_SDK2_BUILD_DIR}"
        UPDATE_COMMAND   ""
        CMAKE_ARGS       ${_LIVOX_SDK2_CMAKE_ARGS}
        BUILD_COMMAND    ${_LIVOX_SDK2_BUILD_CMD}
        INSTALL_COMMAND  ""
        BUILD_BYPRODUCTS "${_LIVOX_SDK2_STATIC_LIB}"
    )
    set(_LIVOX_SDK2_INCLUDE_DIR "${_LIVOX_SDK2_SOURCE_DIR}/include")
endif()

add_library(${TARGET_NAME} SHARED
    livox_backend.cpp
)
add_dependencies(${TARGET_NAME} livox_sdk2_ext)
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include  # dlstreamer/lidar/g3d_lidar_backend_api.h
        ${_LIVOX_SDK2_INCLUDE_DIR}
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        "${_LIVOX_SDK2_STATIC_LIB}"
        json-hpp  # nlohmann/json for params parsing and SDK config generation
        pthread
)

# Keep the statically linked SDK (and its bundled spdlog) private to this
# library: don't re-export its symbols and bind internal references locally.
target_link_options(${TARGET_NAME} PRIVATE "LINKER:--exclude-libs,ALL" "LINKER:-Bsymbolic")

# Installed as a plain shared library; g3dlidarsrc dlopen()s it by name.
install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_LIBRARIES_INSTALL_PATH})
