# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

add_subdirectory(g3dradarprocess)
add_subdirectory(g3dlidarparse)
add_subdirectory(g3dlidarsrc)
add_subdirectory(g3dinference)

# Note: g3dlidarsrc's LiDAR vendor backends (libg3dlidar_<vendor>.so) live under
# g3dlidarsrc/backends/ and are each opt-in (e.g. -DENABLE_LIDAR_ROBOSENSE=ON).
# They are loaded at runtime via dlopen, not linked into the plugin.