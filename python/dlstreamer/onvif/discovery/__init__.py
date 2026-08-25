# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
dlstreamer.onvif.discovery — package shim.

The public API is defined in :mod:`dlstreamer.onvif.discovery.api` and
re-exported here so that existing imports keep working::

    from dlstreamer.onvif.discovery import discover_onvif_cameras
    # equivalent to
    from dlstreamer.onvif.discovery.api import discover_onvif_cameras
"""

from .api import *  # noqa: F401,F403
from .api import __all__  # noqa: F401
