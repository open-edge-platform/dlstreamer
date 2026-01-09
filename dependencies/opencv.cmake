# ==============================================================================
# Copyright (C) 2018-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

include(ExternalProject)

# When changing version, you will also need to change the download hash
set(DESIRED_VERSION 4.12.0)

ExternalProject_Add(
    opencv_contrib
    PREFIX ${CMAKE_BINARY_DIR}/opencv_contrib
    URL     https://github.com/opencv/opencv_contrib/archive/${DESIRED_VERSION}.zip
    URL_MD5 2eecff53ebd74f6291108247d365cb61
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CONFIGURE_COMMAND   ""
    BUILD_COMMAND       ""
    INSTALL_COMMAND     ""
    TEST_COMMAND        ""
)

ExternalProject_Get_Property(opencv_contrib SOURCE_DIR)
ExternalProject_Add(
    opencv
    PREFIX ${CMAKE_BINARY_DIR}/opencv
    URL     https://github.com/opencv/opencv/archive/${DESIRED_VERSION}.zip
    URL_MD5 6bc2ed099ff31451242f37a5f2dac0cf
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CMAKE_GENERATOR     Ninja
    TEST_COMMAND        ""
    INSTALL_COMMAND     ninja install
    CMAKE_ARGS          -DBUILD_TESTS=OFF
                        -DCMAKE_BUILD_TYPE=Release
                        -DOPENCV_GENERATE_PKGCONFIG=ON
                        -DBUILD_SHARED_LIBS=ON
                        -DBUILD_PERF_TESTS=OFF
                        -DBUILD_EXAMPLES=OFF
                        -DBUILD_opencv_apps=OFF
                        -DOPENCV_EXTRA_MODULES_PATH=${SOURCE_DIR}/modules
                        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_BINARY_DIR}/opencv-bin
                        -DWITH_VA=ON
                        -DWITH_VA_INTEL=ON
                        -DWITH_FFMPEG=OFF
                        -DWITH_TBB=ON
                        -DWITH_OPENMP=OFF
                        -DWITH_IPP=OFF
)

if (INSTALL_DLSTREAMER)
    execute_process(COMMAND mkdir -p ${DLSTREAMER_INSTALL_PREFIX}/opencv
                    COMMAND cp -r ${CMAKE_BINARY_DIR}/opencv-bin/. ${DLSTREAMER_INSTALL_PREFIX}/opencv)
endif()
