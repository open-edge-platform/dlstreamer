# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "test_d3d-pre-proc")

find_package(PkgConfig REQUIRED)
find_package(OpenCL REQUIRED)

pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTD3D11 gstreamer-d3d11-1.0>=1.16 REQUIRED)
# GSTD3D12_INCLUDE_DIRS / GSTD3D12_LIBRARIES come from the top-level CMakeLists.txt.

project(${TARGET_NAME})

file(GLOB TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(${TARGET_NAME} ${TEST_SOURCES})

target_compile_definitions(${TARGET_NAME} PRIVATE GST_USE_UNSTABLE_API)

target_link_libraries(${TARGET_NAME}
PRIVATE
    image_inference_async_d3d11
    image_inference_async_d3d12
    gtest
    OpenCL::OpenCL
    d3d11
    d3d12
    ${GSTREAMER_LIBRARIES}
    ${GSTD3D11_LIBRARIES}
    ${GSTD3D12_LIBRARIES}
)
target_include_directories(${TARGET_NAME}
PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTD3D11_INCLUDE_DIRS}
    ${GSTD3D12_INCLUDE_DIRS}
)

add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
