# ==============================================================================
# Copyright (C) 2020-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set (TARGET_NAME "gvapython")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 glib-2.0 REQUIRED)
pkg_check_modules(GSTREAMER gstreamer-1.0>=1.16 REQUIRED)
pkg_check_modules(PYGOBJECT pygobject-3.0)
find_package(PythonLibs 3.4)

if (NOT PYGOBJECT_FOUND OR NOT PYTHONLIBS_FOUND)
    message("Python library not found, skipping gvapython build")
    return()
endif()

file (GLOB MAIN_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    )

file (GLOB MAIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_library(${TARGET_NAME} SHARED ${MAIN_SRC} ${MAIN_HEADERS})
set_compile_flags(${TARGET_NAME})

target_include_directories(${TARGET_NAME}
PRIVATE
    ${GLIB2_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${PYTHON_INCLUDE_DIRS}
    ${PYGOBJECT_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_NAME}
PRIVATE
    ${GSTREAMER_LIBRARIES}
    ${GLIB2_LIBRARIES}
    ${PYTHON_LIBRARIES}
    ${PYGOBJECT_LIBRARIES}
    common
    utils
)

install(TARGETS ${TARGET_NAME} DESTINATION ${DLSTREAMER_PLUGINS_INSTALL_PATH})

if(WIN32)
    find_package(dlfcn-win32 REQUIRED)
    target_include_directories(${TARGET_NAME} PRIVATE ${dlfcn-win32_INCLUDE_DIRS})
    target_link_libraries(${TARGET_NAME} PRIVATE ${dlfcn-win32_LIBRARIES})
endif()
