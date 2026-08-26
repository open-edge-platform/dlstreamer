# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
dlstreamer.onvif.event_manager — package shim.

The public API is defined in
:mod:`dlstreamer.onvif.event_manager.api` and re-exported here so that
existing imports keep working::

    from dlstreamer.onvif.event_manager import find_event_capable_cameras
    # equivalent to
    from dlstreamer.onvif.event_manager.api import find_event_capable_cameras
"""

from .api import *  # noqa: F401,F403
from .api import __all__  # noqa: F401
