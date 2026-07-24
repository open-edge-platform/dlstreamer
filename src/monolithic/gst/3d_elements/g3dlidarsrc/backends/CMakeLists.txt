# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# LiDAR vendor backends for g3dlidarsrc.
#
# Each backend is a standalone shared library that implements the vendor-neutral
# C ABI in <dlstreamer/lidar/g3d_lidar_backend_api.h> and is named
# libg3dlidar_<vendor>.so. g3dlidarsrc dlopen()s the one matching the config's
# `vendor` field at runtime; nothing here is linked into the element or plugin.
#
# Every backend is OPT-IN (default OFF) because building it pulls in that
# vendor's SDK (source fetch/compile or a system package). The element and the
# plugin build fine with all backends off -- g3dlidarsrc is still registered and
# fails at start() only when the requested vendor's backend is not present.
#
# To add a vendor: create backends/<vendor>/ with a CMakeLists.txt producing
# libg3dlidar_<vendor>.so, and add an option + add_subdirectory below.

option(ENABLE_LIDAR_ROBOSENSE "Build the RoboSense (rs_driver) LiDAR backend for g3dlidarsrc" OFF)
if(ENABLE_LIDAR_ROBOSENSE)
    add_subdirectory(robosense)
endif()
