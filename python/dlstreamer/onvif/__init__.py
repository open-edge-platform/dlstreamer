# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
dlstreamer.onvif — ONVIF camera discovery and pipeline integration library.

Public API
----------
Discovery functions (low-level, no GStreamer dependency):
    discover_onvif_cameras()       — synchronous WS-Discovery generator
    discover_onvif_cameras_async() — async WS-Discovery generator

High-level engine (manages discovery loop + ONVIF connection + GStreamer pipelines):
    DlsOnvifDiscoveryEngine

Data models:
    ONVIFProfile            — per-camera media profile (resolution, codec, PTZ, RTSP URL)
    DlsOnvifCameraEntry     — single discovered camera with lifecycle state
    DlsOnvifCameraRegistry  — thread-safe in-memory store of all known cameras
    CameraStatus            — camera lifecycle enum

Pipeline launcher:
    DlsLaunchedPipeline     — wraps a running GStreamer pipeline

Configuration:
    DlsOnvifConfigManager   — loads pipeline definitions from a config.json file

Utility:
    print_cameras           — tabular debug printer
"""

from .dls_onvif_data import ONVIFProfile
from .misc import print_cameras
from .dls_onvif_config_manager import DlsOnvifConfigManager
from .dls_onvif_discovery_thread import DlsLaunchedPipeline
from .dls_onvif_camera_entry import (
    CameraStatus,
    DlsOnvifCameraEntry,
    DlsOnvifCameraRegistry,
)
from .dls_onvif_discovery_engine import (
    DlsOnvifDiscoveryEngine,
    discover_onvif_cameras,
    discover_onvif_cameras_async,
)

__all__ = [
    "ONVIFProfile",
    "print_cameras",
    "DlsOnvifConfigManager",
    "DlsLaunchedPipeline",
    "CameraStatus",
    "DlsOnvifCameraEntry",
    "DlsOnvifCameraRegistry",
    "DlsOnvifDiscoveryEngine",
    "discover_onvif_cameras",
    "discover_onvif_cameras_async",
]
