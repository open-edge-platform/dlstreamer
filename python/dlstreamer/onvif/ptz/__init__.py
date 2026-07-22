# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
dlstreamer.onvif.ptz — package shim.

The public API is defined in :mod:`dlstreamer.onvif.ptz.api` and
re-exported here so that existing imports keep working::

    from dlstreamer.onvif.ptz import PTZController
    # equivalent to
    from dlstreamer.onvif.ptz.api import PTZController
"""

from .api import *  # noqa: F401,F403
from .api import __all__  # noqa: F401
