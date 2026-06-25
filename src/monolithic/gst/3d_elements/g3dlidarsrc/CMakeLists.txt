# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

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
    message(STATUS "g3dlidarsrc: using user-supplied rs_driver at ${RS_DRIVER_SRC_DIR} "
                   "(expected commit: ${RS_DRIVER_VERSION}; patches must be applied manually)")
else()
    # FetchContent path: clone the pinned revision and apply tracked patches.
    # Patches live next to this element (not in dependencies/patches/, which the
    # Dockerfiles git-apply onto gstreamer) so they stay scoped to this consumer.
    include(FetchContent)
    set(_RS_DRIVER_PATCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/patches")
    message(STATUS "g3dlidarsrc: RS_DRIVER_SRC_DIR not set; fetching rs_driver ${RS_DRIVER_VERSION} via FetchContent")
    FetchContent_Declare(
        rs_driver
        GIT_REPOSITORY  https://github.com/RoboSense-LiDAR/rs_driver.git
        GIT_TAG         ${RS_DRIVER_VERSION}
        GIT_SHALLOW     FALSE   # full clone: shallow can fail with a sha tag
        PATCH_COMMAND   bash -c "bash ${_RS_DRIVER_PATCH_DIR}/apply_patch.sh \
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

add_library(g3dlidarsrc STATIC
    g3dlidarsrc.cpp
)
set_compile_flags(g3dlidarsrc)

if(UNIX)
    target_compile_options(g3dlidarsrc PRIVATE
        -Wno-error=unused-variable
        -Wno-error=unused-parameter
        -Wno-error=unused-function
        -Wno-inline
    )
    # Disable treating warnings as errors for this target (due to rs_driver inline function issues)
    target_compile_options(g3dlidarsrc PRIVATE -Wno-error)
endif()

if(WIN32)
    # Windows-specific compile options
    target_compile_options(g3dlidarsrc PRIVATE
        /wd4244  # Disable warning C4244 (conversion warnings from rs_driver)
        /wd4267  # Disable warning C4267 (size_t conversion)
    )
    # Add version resource for Windows DLL
    if(COMMAND add_vs_version_resource)
        add_vs_version_resource(g3dlidarsrc)
    endif()
endif()

# Disable PCAP support in rs_driver (no need for libpcap dependency)
target_compile_definitions(g3dlidarsrc
    PRIVATE
        DISABLE_PCAP_PARSE
)

target_include_directories(g3dlidarsrc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${DLSTREAMER_BASE_DIR}/include/dlstreamer/gst/metadata
    PRIVATE
        ${GSTREAMER_INCLUDE_DIRS}
        ${RS_DRIVER_SRC_DIR}
)

target_link_libraries(g3dlidarsrc
    PUBLIC
        ${GSTREAMER_LIBRARIES}
        ${GSTVIDEO_LIBRARIES}
    PRIVATE
        json-hpp  # nlohmann/json for parsing the LiDAR config file
        $<$<PLATFORM_ID:Linux>:pthread>  # pthread only needed on Linux
)

# Windows-specific libraries
if(WIN32)
    target_link_libraries(g3dlidarsrc
        PRIVATE
            ws2_32  # Winsock2 for network programming
            wsock32 # Additional Windows socket library
    )
endif()
