# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "inference_elements")

find_package(OpenCV REQUIRED core imgproc)
find_package(PkgConfig REQUIRED)
find_package(OpenVINO REQUIRED Runtime)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTVIDEO gstreamer-video-1.0>=1.16 REQUIRED)
pkg_check_modules(GSTALLOC gstreamer-allocators-1.0 REQUIRED)
pkg_check_modules(GSTANALYTICS gstreamer-analytics-1.0>=1.16 REQUIRED)

file (GLOB MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/base/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/base/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/post_processor/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/post_processor/converters/to_roi/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/post_processor/converters/to_tensor/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/post_processor/converters/to_tensor/human_pose_extractor/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gvaclassify/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gvaclassify/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gvadetect/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gvadetect/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gvadetect/converters/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gvainference/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gvainference/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/gvarealsense/*.cpp
)

file (GLOB MAIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/base/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/post_processor/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/post_processor/converters/to_roi/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/post_processor/converters/to_tensor/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/common/post_processor/converters/to_tensor/human_pose_extractor/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gvaclassify/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gvadetect/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gvadetect/converters/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gvainference/*.h
)

add_library(${TARGET_NAME} STATIC ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

# FIXME: there are some debug information that are removed for released build type
# FIXME: hence it marked as error
if(UNIX)
    target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=unused-variable -Wno-error=unused-parameter)
endif()

target_include_directories(${TARGET_NAME}
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/base/
    ${CMAKE_CURRENT_SOURCE_DIR}/common/
    ${CMAKE_CURRENT_SOURCE_DIR}/gvaclassify/
    ${CMAKE_CURRENT_SOURCE_DIR}/gvadetect/
    ${CMAKE_CURRENT_SOURCE_DIR}/gvainference/
    ${CMAKE_CURRENT_SOURCE_DIR}/gvarealsense/
    ${DLSTREAMER_BASE_DIR}/src/base/dlstreamer_logger/
PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTVIDEO_INCLUDE_DIRS}
    ${GSTALLOC_INCLUDE_DIRS}
    ${DLSTREAMER_BASE_DIR}/src/utils
)

target_link_libraries(${TARGET_NAME}
PRIVATE
    ${GSTREAMER_LIBRARIES}
    ${GSTVIDEO_LIBRARIES}
    ${GSTALLOC_LIBRARIES}
    ${GSTANALYTICS_LIBRARIES}
    ${OpenCV_LIBS}
    dlstreamer_api
    dlstreamer_logger
    common
    image_inference
    image_inference_openvino
    pre_proc
    opencv_pre_proc
    logger
PUBLIC
    openvino::runtime
    runtime_feature_toggling
    model_proc
)

if(${ENABLE_VAAPI} AND NOT(WIN32))
    target_link_libraries(${TARGET_NAME} PRIVATE va_api_wrapper image_inference_async)
endif()

if(WIN32)
    find_package(dlfcn-win32 REQUIRED)
    target_include_directories(${TARGET_NAME} PRIVATE ${dlfcn-win32_INCLUDE_DIRS})
    target_link_libraries(${TARGET_NAME} PRIVATE ${dlfcn-win32_LIBRARIES})
    target_link_libraries(${TARGET_NAME} PRIVATE d3d11_wrapper image_inference_async_d3d11)
endif()
