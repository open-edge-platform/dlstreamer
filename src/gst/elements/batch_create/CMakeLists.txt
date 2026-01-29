# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(TARGET_NAME "batch_create")

if(WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSTD3D11 gstreamer-d3d11-1.0)
endif()

file(GLOB MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB MAIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
)

add_library(${TARGET_NAME} STATIC ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME}
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
    target_include_directories(${TARGET_NAME} PUBLIC ${GSTD3D11_INCLUDE_DIRS})
endif()

target_link_libraries(${TARGET_NAME}
PUBLIC
    dlstreamer_gst
    batch_split
)

if(WIN32)
    target_link_libraries(${TARGET_NAME} PUBLIC ${GSTD3D11_LIBRARIES})
endif()

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})
