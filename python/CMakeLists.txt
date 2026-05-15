# ==============================================================================
# Copyright (C) 2022 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/gstgva
        DESTINATION python
        USE_SOURCE_PERMISSIONS
        PATTERN "*.pyc" EXCLUDE
        PATTERN "__pycache__" EXCLUDE
        PATTERN "CMakeLists.txt" EXCLUDE)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/dlstreamer
        DESTINATION python
        USE_SOURCE_PERMISSIONS
        PATTERN "*.pyc" EXCLUDE
        PATTERN "__pycache__" EXCLUDE)
