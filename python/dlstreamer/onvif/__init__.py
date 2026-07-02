# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""dlstreamer.onvif — ONVIF Camera Management (GStreamer / DL Streamer native API).

The 13 operations of the GStreamer / DL Streamer native variant of the
ONVIF Camera Management library, as specified in the HLD
(*Library API — GStreamer / DL Streamer native variant*).

All signatures use only ``Gst.*`` types; no custom dataclasses, handles
or enums are introduced in this layer.

The library is implemented as a ``GstDeviceProvider`` named
``onvifdeviceprovider`` (loaded from the in-tree
``dlstreamer/onvif-plugin/`` sibling) that plugs into the standard
``GstDeviceMonitor`` machinery, so applications interact with ONVIF
cameras exactly the same way they already interact with V4L2 or
PulseAudio sources.

Implementation modules (``dls_onvif_*``, ``misc``, ``_provider``,
``_state``, ``api``) are intentionally **not** re-exported here —
treat them as internal.
"""
from __future__ import annotations

from .api import (
    add_or_update_pipeline,
    get_camera_capabilities,
    get_camera_profiles,
    get_pipeline_definition,
    init_discovery,
    list_defined_pipelines,
    list_discovered_cameras,
    list_pipeline_definitions,
    register_camera_event,
    register_pipeline_definition,
    release_discovery,
    remove_pipeline,
    set_camera_credentials,
    start_discovery,
    stop_discovery,
    unregister_camera_event,
    unregister_pipeline_definition,
)

__all__ = [
    "init_discovery",
    "release_discovery",
    "start_discovery",
    "stop_discovery",
    "list_discovered_cameras",
    "list_defined_pipelines",
    "add_or_update_pipeline",
    "remove_pipeline",
    "register_pipeline_definition",
    "unregister_pipeline_definition",
    "get_pipeline_definition",
    "list_pipeline_definitions",
    "register_camera_event",
    "unregister_camera_event",
    "get_camera_capabilities",
    "get_camera_profiles",
    "set_camera_credentials",
]

__version__ = "0.1.0"


# Auto-register the ``onvifdeviceprovider`` factory on package import so
# that consumers calling :func:`init_discovery` (or any other Gst-based
# consumer that does ``Gst.DeviceMonitor.add_filter("Source/Network/ONVIF")``)
# find the factory ready in the default ``Gst.Registry``.
#
# Skipped inside ``gst-plugin-scanner`` — there the C shim plugin
# (``libgstdlsonvif.so``) owns the registration; auto-registering a
# STATIC plugin here would race and leave the shim with 0 features in
# the binary registry cache.
from . import _provider as _provider_module  # noqa: E402

try:
    _provider_module.ensure_provider_registered()
except Exception:  # pylint: disable=broad-except
    pass
del _provider_module
