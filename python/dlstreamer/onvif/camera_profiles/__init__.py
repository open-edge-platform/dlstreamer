# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
dlstreamer.onvif.camera_profiles — package shim.

The public API is defined in :mod:`dlstreamer.onvif.camera_profiles.api`
and re-exported here so that existing imports keep working::

    from dlstreamer.onvif.camera_profiles import read_camera_profiles
    # equivalent to
    from dlstreamer.onvif.camera_profiles.api import read_camera_profiles
"""

from .api import *  # noqa: F401,F403
from .api import __all__  # noqa: F401
