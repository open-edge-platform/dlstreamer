# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# CPack Configuration for Windows Installers

# Dependency versions and installer SHA256 hashes
set(GSTREAMER_VERSION "1.28.1")
set(GSTREAMER_SHORT_VERSION "1.28")
set(GSTREAMER_INSTALLER_HASH "2ec50356d2d0937a9ead0f99d322f81d8413b9514c9d58ed41ca58fbcf25bfde")
set(PYTHON_VERSION "3.12.10")
set(PYTHON_SHORT_VERSION "3.12")
set(PYTHON_INSTALLER_HASH "67b5635e80ea51072b87941312d00ec8927c4db9ba18938f7ad2d27b328b95fb")

# Default build configuration for CPack
if(NOT CPACK_BUILD_CONFIG)
    set(CPACK_BUILD_CONFIG "Release")
endif()

# Include installation rules
include(${CMAKE_CURRENT_LIST_DIR}/install_targets.cmake)

# Use NSIS generator
set(CPACK_GENERATOR "NSIS")

# System architecture
set(CPACK_SYSTEM_NAME "win64")

# Basic package information
set(CPACK_PACKAGE_NAME "dlstreamer")
set(CPACK_PACKAGE_VENDOR "${PRODUCT_COMPANY_NAME}")
set(CPACK_PACKAGE_VERSION_MAJOR ${VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${VERSION_PATCH})
set(CPACK_PACKAGE_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}")

# Installation directory
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Intel\\\\dlstreamer")

# License files
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# Source package settings
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/"
    "/build/"
    "/\\\\.vs/"
    "\\\\.gitignore"
    "\\\\.gitmodules"
)

# ============================================================================
# NSIS Specific Configuration
# ============================================================================

# NSIS package name
set(CPACK_NSIS_PACKAGE_NAME "${PRODUCT_NAME} ${CPACK_PACKAGE_VERSION}")
set(CPACK_NSIS_DISPLAY_NAME "${PRODUCT_NAME} ${CPACK_PACKAGE_VERSION}")

# Installation directories
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
set(CPACK_NSIS_INSTALLED_ICON_NAME "Uninstall.exe")

# Icons and Header image
# set(CPACK_NSIS_MUI_ICON "'${CMAKE_CURRENT_LIST_DIR}\\\\installer.ico'")
# set(CPACK_NSIS_MUI_UNIICON "'${CMAKE_CURRENT_LIST_DIR}\\\\installer.ico'")

option(NSIS_SKIP_COMPRESSION "NSIS skip compress to speed up build" OFF)
if(NSIS_SKIP_COMPRESSION)
    set(NSIS_COMPRESS_FLAG "off")
else()
    set(NSIS_COMPRESS_FLAG "auto")
endif()

# Code signing
set(CODE_SIGN_SCRIPT "" CACHE FILEPATH "Path to Invoke-CodeSign.ps1 script for code signing, leave empty to skip signing")
if(CODE_SIGN_SCRIPT)
    set(NSIS_SIGN_COMMANDS "
  !uninstfinalize 'powershell -ExecutionPolicy Bypass -File \"${CODE_SIGN_SCRIPT}\" \\\"%1\\\"'
  !finalize 'powershell -ExecutionPolicy Bypass -File \"${CODE_SIGN_SCRIPT}\" \\\"%1\\\"'")
else()
    set(NSIS_SIGN_COMMANDS "")
endif()

# Set extra options
set(CPACK_NSIS_COMPRESSOR "lzma")
set(CPACK_NSIS_DLSTREAMER_DEFINES "
  SetCompressorDictSize 32
  SetCompress ${NSIS_COMPRESS_FLAG}
  ${NSIS_SIGN_COMMANDS}

  ; Installer version info
  BrandingText '${PRODUCT_NAME}'
  VIProductVersion '${CPACK_PACKAGE_VERSION}.0'
  VIAddVersionKey 'ProductName' '${PRODUCT_NAME}'
  VIAddVersionKey 'CompanyName' '${PRODUCT_COMPANY_NAME}'
  VIAddVersionKey 'LegalCopyright' '${PRODUCT_COPYRIGHT}'
  VIAddVersionKey 'FileDescription' '${PRODUCT_NAME} Installer'
  VIAddVersionKey 'FileVersion' '${CPACK_PACKAGE_VERSION}.0'
  VIAddVersionKey 'ProductVersion' '${PRODUCT_FULL_VERSION}'

  !define PACKAGE_FILE_NAME '${CPACK_PACKAGE_FILE_NAME}'
  !define GSTREAMER_VERSION '${GSTREAMER_VERSION}'
  !define GSTREAMER_INSTALLER_HASH '${GSTREAMER_INSTALLER_HASH}'
  !define PYTHON_VERSION '${PYTHON_VERSION}'
  !define PYTHON_SHORT_VERSION '${PYTHON_SHORT_VERSION}'
  !define PYTHON_INSTALLER_HASH '${PYTHON_INSTALLER_HASH}'
  !define INSTALLER_DEPS_DIR '${DLSTREAMER_DEPS_DIR}'

  !addincludedir '${CMAKE_CURRENT_LIST_DIR}'
  !include 'dlstreamer.nsh'
")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/packaging/windows)

include(CPack)

# ============================================================================
# Add custom targets
# ============================================================================

if(CODE_SIGN_SCRIPT)
    add_custom_target(sign_files
        COMMAND powershell -ExecutionPolicy Bypass -File "${CODE_SIGN_SCRIPT}"
            -Directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Signing built binaries"
    )
endif()

add_custom_target(package_all
    COMMAND ${CMAKE_CPACK_COMMAND} -G "NSIS" -C $<IF:$<CONFIG:>,Release,$<CONFIG>>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building Windows installer"
)

if(CODE_SIGN_SCRIPT)
    add_dependencies(package_all sign_files)
endif()
