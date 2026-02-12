# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# CMake script to generate typelib from committed GIR file
# Following GStreamer's approach: GIR files are committed to girs/ folder

find_program(G_IR_COMPILER g-ir-compiler)

if(NOT G_IR_COMPILER)
    message(WARNING "g-ir-compiler not found. GObject Introspection will not be available.")
    return()
endif()

# Set the namespace and version for your GIR file
set(GIR_NAMESPACE "DLStreamerMeta")
set(GIR_VERSION "1.0")
set(GIR_FILENAME "${GIR_NAMESPACE}-${GIR_VERSION}.gir")
set(COMMITTED_GIR "${CMAKE_SOURCE_DIR}/girs/${GIR_FILENAME}")
set(GIR_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${GIR_FILENAME}")
set(TYPELIB_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${GIR_NAMESPACE}-${GIR_VERSION}.typelib")

# Get GStreamer package information
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-analytics-1.0)

# Resolve GIR search paths via pkg-config to find Gst-1.0.gir
pkg_get_variable(GSTREAMER_GIRDIR gstreamer-1.0 girdir)
pkg_get_variable(GSTREAMER_LIBDIR gstreamer-1.0 libdir)

# Option to generate GIR from source (for updating the committed GIR file)
option(GENERATE_GIR_FROM_SOURCE "Generate GIR file from source instead of using committed version" OFF)

if(GENERATE_GIR_FROM_SOURCE)
    message(STATUS "Generating GIR file from source (GENERATE_GIR_FROM_SOURCE=ON)")

    find_program(G_IR_SCANNER g-ir-scanner)
    if(NOT G_IR_SCANNER)
        message(FATAL_ERROR "g-ir-scanner not found but GENERATE_GIR_FROM_SOURCE=ON. Install gobject-introspection-devel.")
    endif()

    # Source and header files
    set(KEYPOINTS_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/gstanalyticskeypointsmtd.c"
    )

    set(KEYPOINTS_HEADERS
        "${CMAKE_SOURCE_DIR}/include/dlstreamer/gst/metadata/gstanalyticskeypointsmtd.h"
    )

    set(LIB_OUTPUT_DIR "${CMAKE_BINARY_DIR}/intel64/${CMAKE_BUILD_TYPE}/lib")

    # Generate the GIR file from source
    add_custom_command(
        OUTPUT ${GIR_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=${LIB_OUTPUT_DIR}:${GSTREAMER_LIBDIR}:$ENV{LD_LIBRARY_PATH}"
            ${G_IR_SCANNER}
            --warn-all
            --warn-error
            --namespace=${GIR_NAMESPACE}
            --nsversion=${GIR_VERSION}
            --identifier-prefix=GstAnalytics
            --symbol-prefix=gst_analytics
            --add-include-path=${GSTREAMER_GIRDIR}
            --include=Gst-1.0
            --include=GstAnalytics-1.0
            --library-path=${LIB_OUTPUT_DIR}
            --library=dlstreamer_gst_meta
            --cflags-begin
            -I${CMAKE_SOURCE_DIR}/include
            ${GSTREAMER_CFLAGS}
            --cflags-end
            --output=${GIR_OUTPUT}
            --pkg=gstreamer-1.0
            --pkg=gstreamer-analytics-1.0
            ${KEYPOINTS_HEADERS}
            ${KEYPOINTS_SOURCES}
        DEPENDS ${KEYPOINTS_SOURCES} ${KEYPOINTS_HEADERS} ${TARGET_NAME}
        COMMENT "Generating GIR file from source for DLStreamerMeta"
    )

    message(STATUS "After build, copy ${GIR_OUTPUT} to ${COMMITTED_GIR} and commit it")
else()
    # Use committed GIR file from girs/ folder (default behavior)
    message(STATUS "Using committed GIR file from girs/ folder")

    if(NOT EXISTS ${COMMITTED_GIR})
        message(FATAL_ERROR "Committed GIR file not found: ${COMMITTED_GIR}\n"
                           "Generate it first with: cmake -DGENERATE_GIR_FROM_SOURCE=ON")
    endif()

    # Copy committed GIR to build directory
    add_custom_command(
        OUTPUT ${GIR_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E copy ${COMMITTED_GIR} ${GIR_OUTPUT}
        DEPENDS ${COMMITTED_GIR}
        COMMENT "Copying committed GIR file from girs/ folder"
    )
endif()

# Compile GIR to typelib
add_custom_command(
    OUTPUT ${TYPELIB_OUTPUT}
    COMMAND ${G_IR_COMPILER}
        --output=${TYPELIB_OUTPUT}
        --includedir=${GSTREAMER_GIRDIR}
        ${GIR_OUTPUT}
    DEPENDS ${GIR_OUTPUT}
    COMMENT "Compiling GIR to typelib"
)

add_custom_target(keypoints_introspection ALL
    DEPENDS ${GIR_OUTPUT} ${TYPELIB_OUTPUT}
)

# Install GIR and typelib files to GStreamer's directories
# Use the same location where GStreamer installs its GIR files
pkg_get_variable(GSTREAMER_PREFIX gstreamer-1.0 prefix)

if(GSTREAMER_PREFIX)
    set(GIR_INSTALL_DIR "${GSTREAMER_PREFIX}/share/gir-1.0")
    set(TYPELIB_INSTALL_DIR "${GSTREAMER_PREFIX}/lib/girepository-1.0")
else()
    set(GIR_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/share/gir-1.0")
    set(TYPELIB_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/lib/girepository-1.0")
endif()

install(FILES ${GIR_OUTPUT}
    DESTINATION ${GIR_INSTALL_DIR}
)
install(FILES ${TYPELIB_OUTPUT}
    DESTINATION ${TYPELIB_INSTALL_DIR}
)
