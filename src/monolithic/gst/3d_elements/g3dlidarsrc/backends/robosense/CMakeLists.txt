# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# RoboSense LiDAR backend: wraps the header-only rs_driver SDK in a shared
# library (libg3dlidar_robosense.so) implementing the vendor-neutral C ABI.
# g3dlidarsrc dlopen()s it at runtime when the config vendor is "robosense".
# It is NOT linked into the element or the plugin, so the rs_driver dependency
# is fully contained here.

set(TARGET_NAME "g3dlidar_robosense")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)

# rs_driver is header-only. Two ways to provide it:
#   A) User-supplied: set the RS_DRIVER_SRC_DIR CMake variable or the
#      environment variable of the same name. The user is responsible for
#      checking out the matching commit (RS_DRIVER_VERSION below) and
#      applying patches/*.patch via patches/apply_patch.sh once.
#   B) Default: leave RS_DRIVER_SRC_DIR unset and FetchContent downloads
#      the pinned commit and applies the patches automatically.

# Bump (and re-verify the patches still apply) when picking up new upstream
# changes; remove patches that upstream has merged.
set(RS_DRIVER_VERSION 4eeadace465023db65a63fc8ef680dfaaca5812c)

if(NOT RS_DRIVER_SRC_DIR AND DEFINED ENV{RS_DRIVER_SRC_DIR})
    set(RS_DRIVER_SRC_DIR "$ENV{RS_DRIVER_SRC_DIR}")
endif()

if(RS_DRIVER_SRC_DIR)
    message(STATUS "g3dlidar_robosense: using user-supplied rs_driver at ${RS_DRIVER_SRC_DIR} "
                   "(expected commit: ${RS_DRIVER_VERSION}; patches must be applied manually)")
else()
    # FetchContent path: clone the pinned revision and apply tracked patches.
    # Patches live next to this backend (not in dependencies/patches/, which the
    # Dockerfiles git-apply onto gstreamer) so they stay scoped to this consumer.
    include(FetchContent)
    set(_RS_DRIVER_PATCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/patches")
    message(STATUS "g3dlidar_robosense: RS_DRIVER_SRC_DIR not set; fetching rs_driver ${RS_DRIVER_VERSION} via FetchContent")
    # The patch step (apply_patch.sh + find/sort) needs a POSIX shell. On Linux
    # bash is on PATH; on Windows it ships with Git for Windows but is not
    # necessarily on PATH, so locate it explicitly instead of relying on PATH.
    find_program(BASH_EXECUTABLE bash
        HINTS "$ENV{ProgramFiles}/Git/bin" "$ENV{ProgramFiles}/Git/usr/bin")
    if(NOT BASH_EXECUTABLE)
        message(FATAL_ERROR "g3dlidar_robosense: 'bash' not found; it is required to apply rs_driver patches. "
                            "Install Git for Windows (which bundles bash) or pass -DBASH_EXECUTABLE=<path>.")
    endif()
    FetchContent_Declare(
        rs_driver
        GIT_REPOSITORY  https://github.com/RoboSense-LiDAR/rs_driver.git
        GIT_TAG         ${RS_DRIVER_VERSION}
        GIT_SHALLOW     FALSE   # full clone: shallow can fail with a sha tag
        PATCH_COMMAND   "${BASH_EXECUTABLE}" -c "bash ${_RS_DRIVER_PATCH_DIR}/apply_patch.sh \
                                 $(find ${_RS_DRIVER_PATCH_DIR} -maxdepth 1 -name '*.patch' -type f | sort)"
    )
    # FetchContent_Populate (not _MakeAvailable): we just want the headers, not
    # rs_driver's demo executables.
    FetchContent_GetProperties(rs_driver)
    if(NOT rs_driver_POPULATED)
        FetchContent_Populate(rs_driver)
    endif()
    set(RS_DRIVER_SRC_DIR "${rs_driver_SOURCE_DIR}/src")
endif()

add_library(${TARGET_NAME} SHARED
    robosense_backend.cpp
)
set_compile_flags(${TARGET_NAME})

if(UNIX)
    target_compile_options(${TARGET_NAME} PRIVATE
        -Wno-error=unused-variable
        -Wno-error=unused-parameter
        -Wno-error=unused-function
        -Wno-inline
    )
    # Disable treating warnings as errors for this target (due to rs_driver inline function issues)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-error)
endif()

if(WIN32)
    # Windows-specific compile options
    target_compile_options(${TARGET_NAME} PRIVATE
        /wd4244  # Disable warning C4244 (conversion warnings from rs_driver)
        /wd4267  # Disable warning C4267 (size_t conversion)
    )
    if(COMMAND add_vs_version_resource)
        add_vs_version_resource(${TARGET_NAME})
    endif()
endif()

# Note: DISABLE_PCAP_PARSE (drop the libpcap dependency) is #defined at the top
# of robosense_backend.cpp, before the rs_driver includes, so it is not repeated
# here.

target_include_directories(${TARGET_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include  # dlstreamer/lidar/g3d_lidar_backend_api.h
        ${GSTREAMER_INCLUDE_DIRS}
        ${RS_DRIVER_SRC_DIR}
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        json-hpp  # nlohmann/json for parsing the vendor-specific params object
        $<$<PLATFORM_ID:Linux>:pthread>  # pthread only needed on Linux
)

# Windows-specific libraries
if(WIN32)
    target_link_libraries(${TARGET_NAME}
        PRIVATE
            ws2_32  # Winsock2 for network programming
            wsock32 # Additional Windows socket library
    )
endif()

# Installed as a plain shared library; g3dlidarsrc dlopen()s it by name.
install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_LIBRARIES_INSTALL_PATH})
