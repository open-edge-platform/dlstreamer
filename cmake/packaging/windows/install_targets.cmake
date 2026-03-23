# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# CPack Install Targets Configuration, defines installation rules

# ============================================================================
# Runtime Component
# ============================================================================

# Install runtime DLLs to bin directory
install(DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/
    DESTINATION bin
    COMPONENT c01_runtime
    FILES_MATCHING
    PATTERN "*.dll"
    # PATTERN "*.exe"
)

# Include Visual C++ runtime
set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT "c01_runtime")
include(InstallRequiredSystemLibraries)

# Install scripts
install(DIRECTORY ${CMAKE_SOURCE_DIR}/scripts/
    DESTINATION scripts
    COMPONENT c01_runtime
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
    PATTERN "CMakeLists.txt" EXCLUDE
)

# Install Python modules
install(DIRECTORY ${CMAKE_SOURCE_DIR}/python/
    DESTINATION python
    COMPONENT c01_runtime
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
    PATTERN "CMakeLists.txt" EXCLUDE
)

# Install gir and typelib files
install(FILES ${CMAKE_BINARY_DIR}/src/gst/metadata/DLStreamerMeta-1.0.gir
    DESTINATION share/gir-1.0
    COMPONENT c01_runtime
    OPTIONAL
)

install(FILES ${CMAKE_BINARY_DIR}/src/gst/metadata/DLStreamerMeta-1.0.typelib
    DESTINATION lib/girepository-1.0
    COMPONENT c01_runtime
    OPTIONAL
)

# ============================================================================
# Environment Variables Component
# ============================================================================

# Create a marker file to force component creation
file(WRITE ${CMAKE_BINARY_DIR}/env.txt "")
install(FILES ${CMAKE_BINARY_DIR}/env.txt
    DESTINATION deps
    COMPONENT c02_env
)

# ============================================================================
# Samples Component
# ============================================================================

# Install sample source files
install(DIRECTORY ${CMAKE_SOURCE_DIR}/samples/
    DESTINATION samples
    COMPONENT c03_samples
    PATTERN "build" EXCLUDE
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
)

# ============================================================================
# Development Component
# ============================================================================

# Install header files
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/
    DESTINATION include
    COMPONENT c04_development
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
)

# Install dlstreamer_gst_meta.lib
install(FILES ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/dlstreamer_gst_meta.lib
    DESTINATION lib
    COMPONENT c04_development
    OPTIONAL
)

# Install pkgconfig files
install(DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/pkgconfig/
    DESTINATION lib/pkgconfig
    COMPONENT c04_development
    FILES_MATCHING
    PATTERN "*.pc"
)

# ============================================================================
# Dependencies
# ============================================================================

set(DLSTREAMER_DEPS_DIR "$ENV{TEMP}/dlstreamer_tmp" CACHE PATH "Directory for downloaded dependencies")

# Create dependencies directory
file(MAKE_DIRECTORY ${DLSTREAMER_DEPS_DIR})

# Custom target to download all dependencies
add_custom_target(download_installer_deps
    COMMAND ${CMAKE_COMMAND} -E echo "=== Downloading Installer Dependencies ==="
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DLSTREAMER_DEPS_DIR}"
    COMMAND ${CMAKE_COMMAND}
        -DGSTREAMER_VERSION=${GSTREAMER_VERSION}
        -DGSTREAMER_INSTALLER_HASH=${GSTREAMER_INSTALLER_HASH}
        -DPYTHON_VERSION=${PYTHON_VERSION}
        -DPYTHON_INSTALLER_HASH=${PYTHON_INSTALLER_HASH}
        -DDLSTREAMER_DEPS_DIR=${DLSTREAMER_DEPS_DIR}
        -P ${CMAKE_CURRENT_LIST_DIR}/download_deps.cmake
)

file(WRITE ${CMAKE_BINARY_DIR}/gstreamer.txt "")
install(FILES ${CMAKE_BINARY_DIR}/gstreamer.txt
    DESTINATION deps
    COMPONENT c00_gstreamer
)

file(WRITE ${CMAKE_BINARY_DIR}/python.txt "")
install(FILES ${CMAKE_BINARY_DIR}/python.txt
    DESTINATION deps
    COMPONENT c00_python
)

# Install OpenVINO runtime DLLs
if(NOT OpenVINOGenAI_DIR)
    message(FATAL_ERROR "OpenVINOGenAI_DIR is not set. Please run setupvars.ps1 in OpenVINO installation.")
endif()
cmake_path(GET OpenVINOGenAI_DIR PARENT_PATH OPENVINO_RUNTIME_DIR)
set(OPENVINO_BIN_DIR "${OPENVINO_RUNTIME_DIR}/bin/intel64/Release")
set(OPENVINO_TBB_DIR "${OPENVINO_RUNTIME_DIR}/3rdparty/tbb/bin")

install(DIRECTORY
    ${OPENVINO_BIN_DIR}/
    DESTINATION bin
    COMPONENT c01_runtime
    FILES_MATCHING
    PATTERN "*"
    PATTERN "*.pdb" EXCLUDE
)

install(DIRECTORY
    ${OPENVINO_TBB_DIR}/
    DESTINATION bin
    COMPONENT c01_runtime
    FILES_MATCHING 
    PATTERN "*.dll"
    PATTERN "*_debug.dll" EXCLUDE
    PATTERN "*.pdb" EXCLUDE
)

# ============================================================================
# Component-based installation
# ============================================================================

# Disable component-based directory structure
set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)

# Define components
include(CPackComponent)
set(CPACK_COMPONENTS_ALL c00_gstreamer c00_python c01_runtime c02_env c03_samples c04_development)

# Define install types
cpack_add_install_type(Full DISPLAY_NAME "Full")
cpack_add_install_type(Typical DISPLAY_NAME "Typical")
cpack_add_install_type(Minimal DISPLAY_NAME "Minimal")

# Define component groups
cpack_add_component_group(dependencies
    DISPLAY_NAME "Dependencies"
    DESCRIPTION "Third-party software required or recommended for DL Streamer"
    EXPANDED
)

cpack_add_component(c00_gstreamer
    DISPLAY_NAME "GStreamer ${GSTREAMER_VERSION}"
    DESCRIPTION "GStreamer multimedia framework ${GSTREAMER_VERSION}, system-wide installation"
    REQUIRED
    GROUP dependencies
    INSTALL_TYPES Full;Typical;Minimal
)

cpack_add_component(c00_python
    DISPLAY_NAME "Python ${PYTHON_VERSION}"
    DESCRIPTION "Python ${PYTHON_VERSION}, system-wide installation"
    DISABLED
    GROUP dependencies
    INSTALL_TYPES Full
)

# Define components
cpack_add_component(c01_runtime
    DISPLAY_NAME "Runtime"
    DESCRIPTION "DL Streamer runtime libraries and plugins"
    REQUIRED
    INSTALL_TYPES Full;Typical;Minimal
)

cpack_add_component(c02_env
    DISPLAY_NAME "Environment Variables"
    DESCRIPTION "Set up DLSTREAMER_DIR, GST_PLUGIN_PATH, and PATH environment variables for current user"
    INSTALL_TYPES Full;Typical
)

cpack_add_component(c03_samples
    DISPLAY_NAME "Samples"
    DESCRIPTION "Sample applications and scripts"
    INSTALL_TYPES Full;Typical
)

cpack_add_component(c04_development
    DISPLAY_NAME "Development Files"
    DESCRIPTION "Header files and import libraries for building C++ applications using DL Streamer"
    INSTALL_TYPES Full;Typical
)
