# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# pylint: disable=invalid-name,wrong-import-position

"""
DL Streamer GStreamer source element backed by ONVIF camera discovery.

Registers `dlsonvifsrc_py`, a `Gst.Bin` that
  1. runs WS-Discovery (via ``dlstreamer.onvif.discover_onvif_cameras``),
  2. picks one camera (filtered by the ``hostname`` / ``port`` properties,
     or the first one found),
  3. connects via ONVIF, retrieves media profiles and reads the RTSP URL
     of profile ``profile-index``,
  4. internally instantiates ``uridecodebin3 ! videoconvert`` against that
     RTSP URL and exposes a single ``src`` ghost pad with raw decoded video.

If the ``rtsp-uri`` property is set, discovery is skipped and the bin behaves
as a thin wrapper around the supplied RTSP location (useful for unit tests
and when the user already knows the stream URL).
"""

from __future__ import annotations

import os
import sys
import threading
from typing import Optional

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
from gi.repository import (  # pylint: disable=no-name-in-module
    Gst,
    GObject,
)

# ---------------------------------------------------------------------------
# Make `dlstreamer.onvif` importable regardless of how the plugin file is
# loaded by libgstpython.so. We walk up from this file looking for a
# `dlstreamer/__init__.py` and add *its parent* to sys.path — that way the
# package becomes importable as ``dlstreamer.onvif`` without polluting
# the top-level namespace with our internal sub-packages (which would
# shadow the unrelated PyPI ``onvif`` library).
# ---------------------------------------------------------------------------
_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_cur = _THIS_DIR
for _ in range(6):
    if os.path.isfile(os.path.join(_cur, "dlstreamer", "__init__.py")):
        if _cur not in sys.path:
            sys.path.insert(0, _cur)
        break
    parent = os.path.dirname(_cur)
    if parent == _cur:
        break
    _cur = parent

try:
    # Internal imports — these modules sit *below* the 13-function Public
    # API of ``dlstreamer.onvif`` and are not re-exported by it.
    from dlstreamer.onvif.dls_onvif_discovery_engine import (  # pylint: disable=import-error
        discover_onvif_cameras,
        DlsOnvifDiscoveryEngine,
    )
    from onvif import ONVIFCamera  # pylint: disable=import-error
    _ONVIF_IMPORT_ERROR: Optional[Exception] = None
except Exception as _exc:  # pylint: disable=broad-except
    discover_onvif_cameras = None  # type: ignore[assignment]
    DlsOnvifDiscoveryEngine = None  # type: ignore[assignment]
    ONVIFCamera = None  # type: ignore[assignment]
    _ONVIF_IMPORT_ERROR = _exc

Gst.init_python()


class OnvifSrc(Gst.Bin):
    """ONVIF-aware video source bin. Discovers a camera, queries its RTSP URL
    via ONVIF, and decodes the stream behind a single ``src`` ghost pad."""

    __gstmetadata__ = (
        "DLS ONVIF Source Python",        # long name
        "Source/Network",                  # classification
        "ONVIF camera discovery + RTSP source (Python)",
        "Intel DLStreamer",
    )

    __gsttemplates__ = (
        Gst.PadTemplate.new(
            "src", Gst.PadDirection.SRC, Gst.PadPresence.ALWAYS, Gst.Caps.new_any()
        ),
    )

    # --- Element properties ---------------------------------------------------
    _hostname = ""
    _port = 0
    _username = ""
    _password = ""
    _profile_index = 0
    _discovery_timeout = 5
    _rtsp_uri = ""
    _latency_ms = 200

    @GObject.Property(type=str, nick="hostname",
                      blurb="If set, only a discovered camera with this hostname/IP is used. "
                            "Empty = pick the first camera found.")
    def hostname(self):
        return self._hostname

    @hostname.setter
    def hostname(self, value):
        self._hostname = value or ""

    @GObject.Property(type=int, nick="port", minimum=0, maximum=65535, default=0,
                      blurb="ONVIF service port filter (0 = any).")
    def port(self):
        return self._port

    @port.setter
    def port(self, value):
        self._port = int(value)

    @GObject.Property(type=str, nick="user-id",
                      blurb="Username used to authenticate against the ONVIF service.")
    def user_id(self):
        return self._username

    @user_id.setter
    def user_id(self, value):
        self._username = value or ""

    @GObject.Property(type=str, nick="user-pw",
                      blurb="Password used to authenticate against the ONVIF service.")
    def user_pw(self):
        return self._password

    @user_pw.setter
    def user_pw(self, value):
        self._password = value or ""

    @GObject.Property(type=int, nick="profile-index", minimum=0, maximum=64, default=0,
                      blurb="Which ONVIF media profile to stream (0-based).")
    def profile_index(self):
        return self._profile_index

    @profile_index.setter
    def profile_index(self, value):
        self._profile_index = int(value)

    @GObject.Property(type=int, nick="discovery-timeout", minimum=1, maximum=60, default=5,
                      blurb="Seconds to wait for WS-Discovery responses.")
    def discovery_timeout(self):
        return self._discovery_timeout

    @discovery_timeout.setter
    def discovery_timeout(self, value):
        self._discovery_timeout = int(value)

    @GObject.Property(type=str, nick="rtsp-uri",
                      blurb="Override: if non-empty, skip ONVIF discovery and stream this RTSP URI directly.")
    def rtsp_uri(self):
        return self._rtsp_uri

    @rtsp_uri.setter
    def rtsp_uri(self, value):
        self._rtsp_uri = value or ""

    @GObject.Property(type=int, nick="latency", minimum=0, maximum=10000, default=200,
                      blurb="RTSP jitter-buffer latency in milliseconds (passed to uridecodebin3).")
    def latency(self):
        return self._latency_ms

    @latency.setter
    def latency(self, value):
        self._latency_ms = int(value)

    # --- Element lifecycle ----------------------------------------------------
    def __init__(self):
        super().__init__()

        self._lock = threading.Lock()
        self._resolved_uri: Optional[str] = None

        # Internal pipeline: uridecodebin3 (dynamic) ! videoconvert ! [ghost src]
        self._uridecodebin = Gst.ElementFactory.make("uridecodebin3", "dlsonvif_uridec")
        self._videoconvert = Gst.ElementFactory.make("videoconvert", "dlsonvif_convert")

        if self._uridecodebin is None or self._videoconvert is None:
            Gst.error("[dlsonvifsrc_py] required GStreamer elements not available "
                      "(need uridecodebin3 and videoconvert)")
            return

        self.add(self._uridecodebin)
        self.add(self._videoconvert)

        # uridecodebin3 has dynamic pads, link them to videoconvert at runtime.
        self._uridecodebin.connect("pad-added", self._on_uridecodebin_pad_added)

        # Always-present ghost src pad fed from videoconvert.
        convert_src = self._videoconvert.get_static_pad("src")
        ghost = Gst.GhostPad.new("src", convert_src)
        ghost.set_active(True)
        self.add_pad(ghost)

    # ------------------------------------------------------------------
    # Discovery + ONVIF profile resolution
    # ------------------------------------------------------------------
    def _resolve_rtsp_uri(self) -> Optional[str]:
        """Return the RTSP URI to stream, performing WS-Discovery + ONVIF
        profile lookup if necessary. Caller must hold no GStreamer locks."""
        if self._rtsp_uri:
            Gst.info(f"[dlsonvifsrc_py] using rtsp-uri override: {self._rtsp_uri}")
            return self._rtsp_uri

        if _ONVIF_IMPORT_ERROR is not None:
            Gst.error(f"[dlsonvifsrc_py] dlstreamer.onvif unavailable: {_ONVIF_IMPORT_ERROR}")
            return None

        camera = self._discover_camera()
        if camera is None:
            return None

        return self._fetch_rtsp_uri(camera["hostname"], camera["port"])

    def _discover_camera(self) -> Optional[dict]:
        """Run WS-Discovery and pick the camera matching hostname/port filters."""
        Gst.info(f"[dlsonvifsrc_py] starting WS-Discovery (timeout={self._discovery_timeout}s, "
                 f"filter hostname='{self._hostname}', port={self._port})")

        match: Optional[dict] = None
        try:
            for cam in discover_onvif_cameras(verbose=False):
                if self._hostname and cam.get("hostname") != self._hostname:
                    continue
                if self._port and int(cam.get("port", 0)) != self._port:
                    continue
                match = cam
                break
        except Exception as exc:  # pylint: disable=broad-except
            Gst.error(f"[dlsonvifsrc_py] WS-Discovery failed: {exc}")
            return None

        if match is None:
            Gst.error("[dlsonvifsrc_py] no ONVIF camera matched the requested filters")
            return None

        Gst.info(f"[dlsonvifsrc_py] discovered camera: {match}")
        return match

    def _fetch_rtsp_uri(self, hostname: str, port: int) -> Optional[str]:
        """Connect to a camera via ONVIF and read the RTSP URL of the chosen profile."""
        try:
            camera = ONVIFCamera(hostname, port, self._username, self._password)
        except Exception as exc:  # pylint: disable=broad-except
            Gst.error(f"[dlsonvifsrc_py] ONVIF connect to {hostname}:{port} failed: {exc}")
            return None

        try:
            # Reuse the engine helper that already parses every media profile.
            engine = DlsOnvifDiscoveryEngine()
            engine.username = self._username
            engine.password = self._password
            engine.verbose = False
            profiles = engine.camera_profiles(camera)
        except Exception as exc:  # pylint: disable=broad-except
            Gst.error(f"[dlsonvifsrc_py] ONVIF GetProfiles failed for {hostname}:{port}: {exc}")
            return None

        if not profiles:
            Gst.error(f"[dlsonvifsrc_py] camera {hostname}:{port} returned zero media profiles")
            return None

        if self._profile_index >= len(profiles):
            Gst.warning(f"[dlsonvifsrc_py] profile-index {self._profile_index} out of range "
                        f"(camera has {len(profiles)} profiles); falling back to 0")
            index = 0
        else:
            index = self._profile_index

        profile = profiles[index]
        rtsp_url = getattr(profile, "rtsp_url", "") or ""

        if not rtsp_url:
            Gst.error(f"[dlsonvifsrc_py] profile '{profile.name}' has no RTSP URL")
            return None

        # Inject credentials so rtspsrc inside uridecodebin3 can authenticate.
        if self._username and self._password and "@" not in rtsp_url:
            scheme, _, rest = rtsp_url.partition("://")
            if scheme and rest:
                rtsp_url = f"{scheme}://{self._username}:{self._password}@{rest}"

        Gst.info(f"[dlsonvifsrc_py] selected profile #{index} '{profile.name}' "
                 f"-> {rtsp_url}")
        return rtsp_url

    # ------------------------------------------------------------------
    # Dynamic pad linking from uridecodebin3
    # ------------------------------------------------------------------
    def _on_uridecodebin_pad_added(self, _src, new_pad):
        """Link only the first video pad uridecodebin3 exposes."""
        caps = new_pad.get_current_caps() or new_pad.query_caps(None)
        if caps is None or caps.get_size() == 0:
            return
        struct_name = caps.get_structure(0).get_name()
        if not struct_name.startswith("video/"):
            return

        sink_pad = self._videoconvert.get_static_pad("sink")
        if sink_pad.is_linked():
            return

        result = new_pad.link(sink_pad)
        if result != Gst.PadLinkReturn.OK:
            Gst.error(f"[dlsonvifsrc_py] failed to link uridecodebin3 -> videoconvert: {result}")

    # ------------------------------------------------------------------
    # State change: resolve the URI on NULL -> READY before children start
    # ------------------------------------------------------------------
    def do_change_state(self, transition):  # pylint: disable=arguments-differ
        if transition == Gst.StateChange.NULL_TO_READY:
            with self._lock:
                if self._resolved_uri is None:
                    uri = self._resolve_rtsp_uri()
                    if not uri:
                        return Gst.StateChangeReturn.FAILURE
                    self._resolved_uri = uri
                    self._uridecodebin.set_property("uri", uri)
                    try:
                        self._uridecodebin.set_property("latency", self._latency_ms)
                    except TypeError:
                        # Older uridecodebin3 versions may not expose `latency`.
                        pass

        if transition == Gst.StateChange.READY_TO_NULL:
            with self._lock:
                self._resolved_uri = None  # allow re-discovery on next cycle

        return Gst.Bin.do_change_state(self, transition)


GObject.type_register(OnvifSrc)
__gstelementfactory__ = ("dlsonvifsrc_py", Gst.Rank.NONE, OnvifSrc)
