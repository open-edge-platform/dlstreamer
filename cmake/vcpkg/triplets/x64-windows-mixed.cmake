# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
if(${PORT} MATCHES "curl|dlfcn-win32|lz4|opencl|openssl|zlib|zstd")
	set(VCPKG_LIBRARY_LINKAGE static)
else()
	set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
