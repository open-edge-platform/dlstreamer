# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# CMake script to generate typelib for Watermark metadata from committed GIR file
# Following GStreamer's approach: GIR files are committed to girs/ folder

find_program(G_IR_COMPILER g-ir-compiler)

if(NOT G_IR_COMPILER)
    message(WARNING "g-ir-compiler not found. GObject Introspection will not be available for watermark metadata.")
    return()
endif()

# Set the namespace and version for watermark GIR file
set(WATERMARK_GIR_NAMESPACE "DLStreamerWatermarkMeta")
set(WATERMARK_GIR_VERSION "1.0")
set(WATERMARK_GIR_FILENAME "${WATERMARK_GIR_NAMESPACE}-${WATERMARK_GIR_VERSION}.gir")
set(WATERMARK_COMMITTED_GIR "${CMAKE_SOURCE_DIR}/girs/${WATERMARK_GIR_FILENAME}")
set(WATERMARK_GIR_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${WATERMARK_GIR_FILENAME}")
set(WATERMARK_TYPELIB_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${WATERMARK_GIR_NAMESPACE}-${WATERMARK_GIR_VERSION}.typelib")

# Get GStreamer package information
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0 gstreamer-video-1.0)

# Resolve GIR search paths via pkg-config
pkg_get_variable(GSTREAMER_GIRDIR gstreamer-1.0 girdir)
pkg_get_variable(GSTREAMER_LIBDIR gstreamer-1.0 libdir)

# Option to generate GIR from source (for updating the committed GIR file)
option(GENERATE_GIR_FROM_SOURCE "Generate watermark GIR file from source instead of using committed version" OFF)

if(GENERATE_GIR_FROM_SOURCE)
    message(STATUS "Generating watermark GIR file from source (GENERATE_GIR_FROM_SOURCE=ON)")

    find_program(G_IR_SCANNER g-ir-scanner)
    if(NOT G_IR_SCANNER)
        message(FATAL_ERROR "g-ir-scanner not found but GENERATE_GIR_FROM_SOURCE=ON. Install gobject-introspection-devel.")
    endif()

    # Watermark metadata source and header files
    set(WATERMARK_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/watermark_draw_meta.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/watermark_circle_meta.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/watermark_text_meta.c"
    )

    set(WATERMARK_HEADERS
        "${CMAKE_SOURCE_DIR}/include/dlstreamer/gst/metadata/watermark_common_meta.h"
        "${CMAKE_SOURCE_DIR}/include/dlstreamer/gst/metadata/watermark_draw_meta.h"
        "${CMAKE_SOURCE_DIR}/include/dlstreamer/gst/metadata/watermark_circle_meta.h"
        "${CMAKE_SOURCE_DIR}/include/dlstreamer/gst/metadata/watermark_text_meta.h"
    )

    set(LIB_OUTPUT_DIR "${CMAKE_BINARY_DIR}/intel64/${CMAKE_BUILD_TYPE}/lib")

    # Generate the watermark GIR file from source
    add_custom_command(
        OUTPUT ${WATERMARK_GIR_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=${LIB_OUTPUT_DIR}:${GSTREAMER_LIBDIR}:$ENV{LD_LIBRARY_PATH}"
            ${G_IR_SCANNER}
            --warn-all
            --namespace=${WATERMARK_GIR_NAMESPACE}
            --nsversion=${WATERMARK_GIR_VERSION}
            --identifier-prefix=Watermark
            --symbol-prefix=watermark
            --add-include-path=${GSTREAMER_GIRDIR}
            --include=Gst-1.0
            --include=GstVideo-1.0
            --library-path=${LIB_OUTPUT_DIR}
            --library=dlstreamer_gst_meta
            --cflags-begin
            -I${CMAKE_SOURCE_DIR}/include
            ${GSTREAMER_CFLAGS}
            --cflags-end
            --output=${WATERMARK_GIR_OUTPUT}
            --pkg=gstreamer-1.0
            --pkg=gstreamer-video-1.0
            ${WATERMARK_HEADERS}
            ${WATERMARK_SOURCES}
        DEPENDS ${WATERMARK_SOURCES} ${WATERMARK_HEADERS} ${TARGET_NAME}
        COMMENT "Generating GIR file from source for DLStreamerWatermarkMeta"
    )

    message(STATUS "After build, copy ${WATERMARK_GIR_OUTPUT} to ${WATERMARK_COMMITTED_GIR} and commit it")
else()
    # Use committed GIR file from girs/ folder (default behavior)
    message(STATUS "Using committed watermark GIR file from girs/ folder")

    if(NOT EXISTS ${WATERMARK_COMMITTED_GIR})
        message(STATUS "Watermark GIR file not found: ${WATERMARK_COMMITTED_GIR}")
        message(STATUS "Generate it first with: cmake -DGENERATE_GIR_FROM_SOURCE=ON")
        return()
    endif()

    # Copy committed watermark GIR to build directory
    add_custom_command(
        OUTPUT ${WATERMARK_GIR_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E copy ${WATERMARK_COMMITTED_GIR} ${WATERMARK_GIR_OUTPUT}
        DEPENDS ${WATERMARK_COMMITTED_GIR}
        COMMENT "Copying committed watermark GIR file from girs/ folder"
    )
endif()

# Compile watermark GIR to typelib
# On Windows, override the shared-library name to dlstreamer_gst_meta.dll.
set(TYPELIB_SHARED_LIB_ARGS)
if(WIN32)
    set(TYPELIB_SHARED_LIB_ARGS --shared-library=dlstreamer_gst_meta.dll)
endif()

add_custom_command(
    OUTPUT ${WATERMARK_TYPELIB_OUTPUT}
    COMMAND ${G_IR_COMPILER}
        --output=${WATERMARK_TYPELIB_OUTPUT}
        --includedir=${GSTREAMER_GIRDIR}
        ${TYPELIB_SHARED_LIB_ARGS}
        ${WATERMARK_GIR_OUTPUT}
    DEPENDS ${WATERMARK_GIR_OUTPUT}
    COMMENT "Compiling watermark GIR to typelib"
)

add_custom_target(watermark_introspection ALL
    DEPENDS ${WATERMARK_GIR_OUTPUT} ${WATERMARK_TYPELIB_OUTPUT}
)

# Install watermark GIR and typelib files to GStreamer's directories
set(GIR_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/share/gir-1.0")
set(TYPELIB_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/lib/girepository-1.0")

install(FILES ${WATERMARK_GIR_OUTPUT}
    DESTINATION ${GIR_INSTALL_DIR}
)
install(FILES ${WATERMARK_TYPELIB_OUTPUT}
    DESTINATION ${TYPELIB_INSTALL_DIR}
)
