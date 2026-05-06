# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

"""Register DLStreamerMeta metadata types (GroupMtd, KeypointMtd) with the
GstAnalytics Python override so that the RelationMeta iterator wraps them
into the correct Python types and isinstance() checks work.

Import this module once before iterating over GstAnalytics.RelationMeta
that may contain DLStreamerMeta metadata::

    import gstgva.meta_registry          # side-effect: registers types
    # or
    from gstgva import meta_registry     # same effect
"""

import gi

gi.require_version("GstAnalytics", "1.0")
gi.require_version("DLStreamerMeta", "1.0")

# pylint: disable=no-name-in-module
from gi.repository import DLStreamerMeta
from gi.overrides import GstAnalytics as _GstAnalyticsOverride
# pylint: enable=no-name-in-module

# pylint: disable=protected-access
_GstAnalyticsOverride._wrap_mtd(
    DLStreamerMeta,
    'KeypointMtd',
    DLStreamerMeta.relation_meta_get_keypoint_mtd
)
_GstAnalyticsOverride._wrap_mtd(
    DLStreamerMeta,
    'GroupMtd',
    DLStreamerMeta.relation_meta_get_group_mtd
)
# pylint: enable=protected-access
