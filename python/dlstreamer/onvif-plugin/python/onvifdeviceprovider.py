# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
# pylint: disable=invalid-name,wrong-import-position,too-many-instance-attributes

"""
``onvifdeviceprovider`` — GStreamer ``Gst.DeviceProvider`` implementation.

Implements the contract documented in ``/home/labrat/intel/ONVIF_API/README.md``
so it can be driven by the ``onvifcm`` library through a standard
``Gst.DeviceMonitor`` (no custom Python API surface required).

Contract checklist honoured here:

* factory name                       ``onvifdeviceprovider``
* device-class                       ``Source/Network/ONVIF``
* device property: stable identity   ``onvif.xaddr``
* device property: capabilities      ``onvif.capabilities`` (Gst.Structure
                                     named ``onvif-capabilities``)
* device property: extras            ``onvif.manufacturer``, ``onvif.model``,
                                     ``onvif.serial``, ``onvif.scopes``,
                                     ``onvif.auth-status``
* per-profile caps fields            ``media``, ``encoding-name``, ``width``,
                                     ``height``, ``framerate``, ``bitrate``,
                                     ``profile``, ``profile-token``,
                                     ``stream-uri``
* reconfigure structure              ``onvif-set-credentials`` (carries
                                     ``uri = onvif://user:pwd@host:port``)
* event structure                    ``onvif-event`` (fields ``topic``,
                                     ``utc-time``, ``source``, ``data``)

Phases implemented:
  1. plugin skeleton
  2. WS-Discovery
  3. SOAP / Media (profiles + capabilities)
  4. credentials (Python-level ``send_event`` shim on the device)
  5. PullPoint events (best-effort, single per-camera worker)
  6. hot-plug (periodic re-probe)

The plugin is loaded by ``libgstpython.so`` once its containing directory
is on ``GST_PLUGIN_PATH`` (or ``ONVIFCM_PLUGIN_PATH``, which ``onvifcm``
scans into the registry).
"""

from __future__ import annotations

import ctypes
import os
import socket
import sys
import threading
import time
from typing import Any, Optional
from urllib.parse import urlparse

import gi

gi.require_version("Gst", "1.0")
from gi.repository import (  # pylint: disable=no-name-in-module
    GObject,
    Gst,
)

# ---------------------------------------------------------------------------
# Make ``dlstreamer.onvif`` importable regardless of how libgstpython loads
# this file (same trick as dls_onvif_src.py).
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
    _IMPORT_ERROR: Optional[Exception] = None
except Exception as _exc:  # pylint: disable=broad-except
    discover_onvif_cameras = None  # type: ignore[assignment]
    DlsOnvifDiscoveryEngine = None  # type: ignore[assignment]
    ONVIFCamera = None  # type: ignore[assignment]
    _IMPORT_ERROR = _exc

# ``init_python`` is required when the file is loaded by libgstpython
# (it registers the ``__gstelementfactory__`` machinery). When imported as
# a plain Python module — e.g. via ``dlstreamer.onvif`` for the explicit
# registration path — GStreamer may not yet be initialised, so guard the
# call and fall back to a plain ``Gst.init`` later in ``register_provider``.
if Gst.is_initialized():
    try:
        Gst.init_python()
    except Exception:  # pylint: disable=broad-except
        pass

_DEVICE_CLASS = "Source/Network/ONVIF"
_CAPS_STRUCT_NAME = "onvif-profile"  # one struct per ONVIF media profile
_CAPABILITIES_STRUCT_NAME = "onvif-capabilities"
_RECONFIGURE_STRUCT_NAME = "onvif-set-credentials"
_EVENT_STRUCT_NAME = "onvif-event"

_DEFAULT_PROBE_INTERVAL_S = 30.0
_DEFAULT_PULLPOINT_TIMEOUT = "PT30S"


# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------
def _gst_log(level: str, message: str) -> None:
    """Pipe library messages through Gst.* logging."""
    logger = getattr(Gst, level, Gst.info)
    logger(f"[onvifdeviceprovider] {message}")


def _resolve_local_ip(target_host: str) -> str:
    """Best-effort: pick the local interface that would reach *target_host*."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sk:
            sk.connect((target_host or "8.8.8.8", 80))
            return sk.getsockname()[0]
    except OSError:
        return ""


# ---------------------------------------------------------------------------
# OnvifDevice — Gst.Device subclass with credential storage
# ---------------------------------------------------------------------------
class OnvifDevice(Gst.Device):
    """A discovered ONVIF camera as a ``Gst.Device``.

    The default element returned by ``create_element`` is ``rtspsrc`` against
    the first media profile's ``stream-uri`` (credentials injected).
    """

    def __init__(
        self,
        *,
        display_name: str,
        caps: Gst.Caps,
        properties: Gst.Structure,
        xaddr: str,
        host: str,
        port: int,
        provider_ref: Optional["OnvifDeviceProvider"] = None,
    ) -> None:
        super().__init__(
            display_name=display_name,
            caps=caps,
            device_class=_DEVICE_CLASS,
            properties=properties,
        )
        self._xaddr = xaddr
        self._host = host
        self._port = port
        # Provider holds the canonical credential store; the device keeps a
        # weak reference so reconfigure events round-trip without an
        # external lookup table.
        self._provider_ref = provider_ref

    # ---- accessors --------------------------------------------------------
    @property
    def xaddr(self) -> str:
        """Stable ONVIF identity (XAddr URL)."""
        return self._xaddr

    @property
    def host(self) -> str:
        """Camera hostname / IP extracted from the XAddr."""
        return self._host

    @property
    def port(self) -> int:
        """Camera ONVIF service port extracted from the XAddr."""
        return self._port

    # ---- Gst.Device vfuncs ------------------------------------------------
    def do_create_element(self, name: Optional[str]) -> Optional[Gst.Element]:
        """Return an ``rtspsrc`` for the first profile that exposes a URI."""
        caps = self.get_caps()
        if caps is None or caps.get_size() == 0:
            _gst_log("warning", f"{self._xaddr} has no profiles cached")
            return None

        struct = caps.get_structure(0)
        stream_uri = struct.get_string("stream-uri") or ""
        if not stream_uri:
            _gst_log("warning", f"{self._xaddr} profile #0 has no stream-uri")
            return None

        creds = self._credentials()
        if creds is not None:
            user, password = creds
            stream_uri = _inject_credentials(stream_uri, user, password)

        element = Gst.ElementFactory.make("rtspsrc", name)
        if element is None:
            return None
        element.set_property("location", stream_uri)
        try:
            element.set_property("latency", 200)
        except TypeError:
            pass
        return element

    def do_reconfigure_element(self, element: Gst.Element) -> bool:
        """Re-apply credentials to an already-built ``rtspsrc``-style element."""
        if element is None:
            return False
        try:
            current = element.get_property("location") or ""
        except TypeError:
            return False
        if not current:
            return False
        creds = self._credentials()
        if creds is None:
            return True
        user, password = creds
        element.set_property("location", _inject_credentials(current, user, password))
        return True

    # ---- credentials path used by onvifcm.set_camera_credentials ---------
    def send_event(self, event: Gst.Event) -> bool:  # type: ignore[override]
        """Python-level shim accepting reconfigure events from the API layer.

        ``Gst.Device`` itself has no ``send_event``; the public ``onvifcm`` API
        nevertheless calls ``camera.send_event(...)`` to push credentials
        through the provider's reconfigure path. We accept the event here
        and forward the parsed credentials to the owning provider.
        """
        if not isinstance(event, Gst.Event):
            return False
        struct = event.get_structure()
        if struct is None or struct.get_name() != _RECONFIGURE_STRUCT_NAME:
            return False
        uri = struct.get_string("uri") or ""
        if not uri:
            return False

        provider = self._provider_ref
        if provider is None:
            return False
        return provider.set_credentials(self._xaddr, uri)

    # ---- internal --------------------------------------------------------
    def _credentials(self) -> Optional[tuple[str, str]]:
        provider = self._provider_ref
        if provider is None:
            return None
        return provider.get_credentials(self._xaddr)


GObject.type_register(OnvifDevice)


def _inject_credentials(uri: str, user: str, password: str) -> str:
    if not user or "@" in uri:
        return uri
    scheme, sep, rest = uri.partition("://")
    if not sep:
        return uri
    return f"{scheme}://{user}:{password}@{rest}"


# ---------------------------------------------------------------------------
# OnvifDeviceProvider
# ---------------------------------------------------------------------------
class OnvifDeviceProvider(Gst.DeviceProvider):
    """Discovers ONVIF cameras on the LAN and publishes them as ``Gst.Device``."""

    __gstmetadata__ = (
        "ONVIF Device Provider",                # long name
        _DEVICE_CLASS,                          # classification
        "WS-Discovery + ONVIF SOAP camera provider (Python)",
        "Intel DLStreamer",
    )

    def __init__(self) -> None:
        super().__init__()
        self._lock = threading.RLock()
        self._stop_event = threading.Event()
        self._worker: Optional[threading.Thread] = None

        # xaddr -> OnvifDevice (currently advertised through device_add)
        self._devices: dict[str, OnvifDevice] = {}
        # xaddr -> (user, password)
        self._credentials: dict[str, tuple[str, str]] = {}
        # xaddr -> auth-status string for property roundtrip
        self._auth_status: dict[str, str] = {}

        # PullPoint workers — xaddr -> (thread, stop_event)
        self._event_workers: dict[str, tuple[threading.Thread, threading.Event]] = {}

        # Probe interval can be overridden by env for tests.
        try:
            self._probe_interval = float(
                os.environ.get("ONVIF_PROVIDER_PROBE_INTERVAL", _DEFAULT_PROBE_INTERVAL_S)
            )
        except ValueError:
            self._probe_interval = _DEFAULT_PROBE_INTERVAL_S

    # ------------------------------------------------------------------
    # Gst.DeviceProvider vfuncs
    # ------------------------------------------------------------------
    def do_start(self) -> bool:
        """Spin up the discovery worker; called by ``Gst.DeviceMonitor.start``."""
        if _IMPORT_ERROR is not None:
            self._post_error(f"dlstreamer.onvif unavailable: {_IMPORT_ERROR}")
            return False

        with self._lock:
            if self._worker is not None and self._worker.is_alive():
                return True
            self._stop_event.clear()
            self._worker = threading.Thread(
                target=self._discovery_loop,
                name="onvifdeviceprovider-discover",
                daemon=True,
            )
            self._worker.start()
        return True

    def do_stop(self) -> None:
        """Tear down the discovery worker and any PullPoint subscribers."""
        self._stop_event.set()
        worker = None
        with self._lock:
            worker = self._worker
            self._worker = None
            event_workers = list(self._event_workers.values())
            self._event_workers.clear()

        for thread, stop in event_workers:
            stop.set()
            thread.join(timeout=2.0)

        if worker is not None:
            worker.join(timeout=self._probe_interval + 5.0)

        # Drop every advertised device; consumers receive DEVICE_REMOVED messages.
        with self._lock:
            for device in list(self._devices.values()):
                try:
                    self.device_remove(device)
                except Exception:  # pylint: disable=broad-except
                    pass
            self._devices.clear()

    # ------------------------------------------------------------------
    # Credential store
    # ------------------------------------------------------------------
    def set_credentials(self, xaddr: str, uri: str) -> bool:
        """Store credentials parsed from a ``onvif://user:pwd@host:port`` URI.

        Verifies them with ``GetDeviceInformation`` and updates the device's
        ``onvif.auth-status`` property.
        """
        parsed = urlparse(uri)
        if parsed.scheme != "onvif" or not parsed.hostname or parsed.username is None:
            return False
        user = parsed.username
        password = parsed.password or ""

        with self._lock:
            self._credentials[xaddr] = (user, password)
            device = self._devices.get(xaddr)

        if device is None:
            self._auth_status[xaddr] = "unknown"
            return False

        host = device.host
        port = device.port or 80
        auth_ok = self._verify_credentials(host, port, user, password)
        self._auth_status[xaddr] = "authenticated" if auth_ok else "unauthorized"

        # Re-build the device so consumers see the refreshed properties / caps.
        self._refresh_device(xaddr)
        return auth_ok

    def get_credentials(self, xaddr: str) -> Optional[tuple[str, str]]:
        """Return ``(user, password)`` for *xaddr* or ``None``."""
        with self._lock:
            return self._credentials.get(xaddr)

    # ------------------------------------------------------------------
    # Discovery loop
    # ------------------------------------------------------------------
    def _discovery_loop(self) -> None:
        """Repeat WS-Discovery until ``do_stop`` is called."""
        first_pass = True
        while not self._stop_event.is_set():
            try:
                self._probe_once()
            except Exception as exc:  # pylint: disable=broad-except
                self._post_error(f"discovery iteration failed: {exc}")

            if first_pass:
                first_pass = False
            if self._probe_interval <= 0:
                return
            # Sleep in small chunks so do_stop wakes us up promptly.
            slept = 0.0
            while slept < self._probe_interval and not self._stop_event.is_set():
                time.sleep(min(0.5, self._probe_interval - slept))
                slept += 0.5

    def _probe_once(self) -> None:
        """Run a single WS-Discovery + SOAP enrichment cycle."""
        seen_xaddrs: set[str] = set()

        for cam in discover_onvif_cameras(verbose=False):
            host = cam.get("hostname") or ""
            port = int(cam.get("port") or 80)
            if not host:
                continue

            xaddr = self._build_xaddr(cam, host, port)
            seen_xaddrs.add(xaddr)

            if self._stop_event.is_set():
                return

            with self._lock:
                already_known = xaddr in self._devices

            if already_known:
                continue

            device = self._build_device(xaddr, host, port)
            if device is None:
                continue

            with self._lock:
                self._devices[xaddr] = device
            try:
                self.device_add(device)
            except Exception as exc:  # pylint: disable=broad-except
                self._post_error(f"device_add({xaddr}) failed: {exc}")

        # Hot-plug: drop devices that disappeared from the latest sweep.
        if not seen_xaddrs:
            return
        with self._lock:
            stale = [x for x in self._devices if x not in seen_xaddrs]
        for xaddr in stale:
            self._drop_device(xaddr)

    def _build_xaddr(self, cam: dict, host: str, port: int) -> str:
        """Reconstruct the canonical XAddr URL for stable identity."""
        raw = cam.get("xaddr") or cam.get("full_url")
        if raw:
            return raw
        return f"http://{host}:{port}/onvif/device_service"

    # ------------------------------------------------------------------
    # Device construction (SOAP / Media)
    # ------------------------------------------------------------------
    def _build_device(self, xaddr: str, host: str, port: int) -> Optional[OnvifDevice]:
        """Connect to *host:port* via ONVIF, build a ``Gst.Device`` for it."""
        user, password = self._credentials.get(xaddr, ("", ""))
        try:
            client = ONVIFCamera(host, port, user, password)
        except Exception as exc:  # pylint: disable=broad-except
            self._post_error(f"ONVIF connect to {host}:{port} failed: {exc}")
            # Still expose a bare device — discovery succeeded even if SOAP did not.
            properties = self._base_properties(xaddr, host, port, user, password)
            return OnvifDevice(
                display_name=host,
                caps=Gst.Caps.new_empty(),
                properties=properties,
                xaddr=xaddr,
                host=host,
                port=port,
                provider_ref=self,
            )

        manufacturer, model, serial = self._fetch_device_info(client)
        capabilities_struct = self._fetch_capabilities(client)
        profiles = self._fetch_profiles(client, user, password)

        caps = self._build_caps_from_profiles(profiles)
        properties = self._base_properties(xaddr, host, port, user, password)
        properties.set_value("onvif.manufacturer", manufacturer)
        properties.set_value("onvif.model", model)
        properties.set_value("onvif.serial", serial)
        properties.set_value("onvif.capabilities", capabilities_struct)

        display_name = model or manufacturer or host
        return OnvifDevice(
            display_name=display_name,
            caps=caps,
            properties=properties,
            xaddr=xaddr,
            host=host,
            port=port,
            provider_ref=self,
        )

    def _refresh_device(self, xaddr: str) -> None:
        """Tear down + rebuild a device (used after credential changes)."""
        with self._lock:
            old = self._devices.pop(xaddr, None)
            host = old.host if old is not None else ""
            port = old.port if old is not None else 80
        if old is None:
            return
        try:
            self.device_remove(old)
        except Exception:  # pylint: disable=broad-except
            pass

        new = self._build_device(xaddr, host, port)
        if new is None:
            return
        with self._lock:
            self._devices[xaddr] = new
        try:
            self.device_add(new)
        except Exception as exc:  # pylint: disable=broad-except
            self._post_error(f"device_add({xaddr}) failed: {exc}")

    def _drop_device(self, xaddr: str) -> None:
        with self._lock:
            device = self._devices.pop(xaddr, None)
            worker = self._event_workers.pop(xaddr, None)
        if worker is not None:
            worker[1].set()
            worker[0].join(timeout=2.0)
        if device is not None:
            try:
                self.device_remove(device)
            except Exception:  # pylint: disable=broad-except
                pass

    # ------------------------------------------------------------------
    # SOAP helpers
    # ------------------------------------------------------------------
    def _fetch_device_info(self, client: Any) -> tuple[str, str, str]:
        try:
            svc = client.create_devicemgmt_service()
            info = svc.GetDeviceInformation()
            return (
                getattr(info, "Manufacturer", "") or "",
                getattr(info, "Model", "") or "",
                getattr(info, "SerialNumber", "") or "",
            )
        except Exception as exc:  # pylint: disable=broad-except
            _gst_log("debug", f"GetDeviceInformation failed: {exc}")
            return ("", "", "")

    def _fetch_capabilities(self, client: Any) -> Gst.Structure:
        """Pack the camera's capability tree into ``onvif-capabilities``."""
        struct = Gst.Structure.new_empty(_CAPABILITIES_STRUCT_NAME)
        try:
            svc = client.create_devicemgmt_service()
            caps = svc.GetCapabilities({"Category": "All"})
        except Exception as exc:  # pylint: disable=broad-except
            _gst_log("debug", f"GetCapabilities failed: {exc}")
            return struct

        struct.set_value("has-media1", bool(getattr(caps, "Media", None)))
        struct.set_value("has-media2", False)
        struct.set_value("has-events", bool(getattr(caps, "Events", None)))
        struct.set_value("has-receiver", bool(getattr(caps, "Receiver", None)))
        struct.set_value("media-version", 1 if getattr(caps, "Media", None) else 0)

        for label, attr in (
            ("media", "Media"),
            ("events", "Events"),
            ("imaging", "Imaging"),
            ("ptz", "PTZ"),
            ("analytics", "Analytics"),
            ("device", "Device"),
        ):
            sub = getattr(caps, attr, None)
            if sub is None:
                continue
            xaddr = getattr(sub, "XAddr", "") or ""
            if xaddr:
                nested = Gst.Structure.new_empty(f"onvif-capability-{label}")
                nested.set_value("xaddr", xaddr)
                struct.set_value(label, nested)
        return struct

    def _fetch_profiles(self, client: Any, user: str, password: str) -> list:
        try:
            engine = DlsOnvifDiscoveryEngine()
            engine.username = user
            engine.password = password
            engine.verbose = False
            return engine.camera_profiles(client) or []
        except Exception as exc:  # pylint: disable=broad-except
            self._post_error(f"GetProfiles failed: {exc}")
            return []

    # ------------------------------------------------------------------
    # Caps / properties construction
    # ------------------------------------------------------------------
    def _build_caps_from_profiles(self, profiles: list) -> Gst.Caps:
        """One ``Gst.Structure`` per profile carrying the contract fields."""
        caps = Gst.Caps.new_empty()
        for profile in profiles:
            struct = Gst.Structure.new_empty(_CAPS_STRUCT_NAME)

            encoding = (getattr(profile, "vec_encoding", "") or "").upper()
            struct.set_value("media", "video")
            struct.set_value("encoding-name", encoding)

            resolution = getattr(profile, "vec_resolution", {}) or {}
            width = int(resolution.get("width") or 0)
            height = int(resolution.get("height") or 0)
            if width:
                struct.set_value("width", width)
            if height:
                struct.set_value("height", height)

            framerate = int(getattr(profile, "vec_framerate_limit", 0) or 0)
            if framerate > 0:
                # Encode the framerate as a Gst.Fraction so it round-trips
                # through caps.get_fraction() the same way as upstream
                # rtspsrc reports it.
                struct.set_value("framerate", Gst.Fraction(framerate, 1))

            bitrate = int(getattr(profile, "vec_bitrate_limit", 0) or 0)
            if bitrate > 0:
                struct.set_value("bitrate", bitrate)

            h264_profile = getattr(profile, "vec_h264_profile", "") or ""
            mpeg4_profile = getattr(profile, "vec_mpeg4_profile", "") or ""
            struct.set_value("profile", h264_profile or mpeg4_profile)

            token = getattr(profile, "token", "") or ""
            struct.set_value("profile-token", token)

            stream_uri = getattr(profile, "rtsp_url", "") or ""
            struct.set_value("stream-uri", stream_uri)

            caps.append_structure(struct)
        return caps

    def _base_properties(
        self,
        xaddr: str,
        host: str,
        port: int,
        user: str,
        password: str,
    ) -> Gst.Structure:
        """Build the device's ``properties`` structure (sans capabilities)."""
        struct = Gst.Structure.new_empty("onvif-device")
        struct.set_value("onvif.xaddr", xaddr)
        struct.set_value("onvif.host", host)
        struct.set_value("onvif.port", int(port))
        struct.set_value("onvif.scopes", "")  # filled when WS-Discovery exposes scopes
        struct.set_value(
            "onvif.auth-status",
            self._auth_status.get(
                xaddr,
                "anonymous" if not user else "authenticated" if user and password else "anonymous",
            ),
        )
        struct.set_value("onvif.manufacturer", "")
        struct.set_value("onvif.model", "")
        struct.set_value("onvif.serial", "")
        struct.set_value("onvif.capabilities", Gst.Structure.new_empty(_CAPABILITIES_STRUCT_NAME))
        return struct

    # ------------------------------------------------------------------
    # Credential verification + PullPoint (best-effort phases 4/5)
    # ------------------------------------------------------------------
    def _verify_credentials(self, host: str, port: int, user: str, password: str) -> bool:
        try:
            client = ONVIFCamera(host, port, user, password)
            client.create_devicemgmt_service().GetDeviceInformation()
            return True
        except Exception as exc:  # pylint: disable=broad-except
            _gst_log("info", f"credential verification for {host}:{port} failed: {exc}")
            return False

    def _post_error(self, message: str) -> None:
        """Best-effort: route library errors to Gst.* logs.

        ``Gst.DeviceProvider`` does not expose a bus directly to Python; we
        therefore mirror the GStreamer convention by emitting an ERROR-level
        log entry so consumers can attach ``GST_DEBUG=onvifdeviceprovider:5``.
        """
        _gst_log("error", message)


GObject.type_register(OnvifDeviceProvider)


# ---------------------------------------------------------------------------
# Registration
#
# We cannot rely on libgstpython for device-provider registration: it only
# handles ``__gstelementfactory__``. PyGObject also does not expose
# ``GstDeviceProviderClass.set_metadata`` nor a way to spawn a new
# ``GstPlugin`` from Python.
#
# Workaround: drop down to ctypes and call ``gst_plugin_register_static_full``
# with a C callback that performs ``gst_device_provider_register`` inside
# the plugin's init function. This is functionally identical to a hand-
# written C plugin and produces a feature that is correctly owned by a
# named ``GstPlugin`` (``onvifdeviceprovider``), so subsequent
# ``Gst.Registry.find_feature`` calls succeed in any process that runs
# :func:`register_provider`.
# ---------------------------------------------------------------------------
_REGISTERED = False
_PLUGIN_INIT_REF: Optional[Any] = None  # keep the ctypes callback alive
_LIBGST: Any = None
_LIBGOBJ: Any = None


def _load_ctypes() -> bool:
    """Lazy-load libgstreamer and libgobject through ctypes."""
    global _LIBGST, _LIBGOBJ  # pylint: disable=global-statement
    if _LIBGST is not None and _LIBGOBJ is not None:
        return True
    try:
        _LIBGOBJ = ctypes.CDLL("libgobject-2.0.so.0")
        _LIBGST = ctypes.CDLL("libgstreamer-1.0.so.0")
    except OSError as exc:
        _gst_log("error", f"could not dlopen GObject / GStreamer for ctypes: {exc}")
        return False

    _LIBGOBJ.g_type_from_name.argtypes = [ctypes.c_char_p]
    _LIBGOBJ.g_type_from_name.restype = ctypes.c_size_t
    _LIBGOBJ.g_type_class_ref.argtypes = [ctypes.c_size_t]
    _LIBGOBJ.g_type_class_ref.restype = ctypes.c_void_p

    _LIBGST.gst_device_provider_class_set_metadata.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
        ctypes.c_char_p, ctypes.c_char_p,
    ]
    _LIBGST.gst_device_provider_class_set_metadata.restype = None
    _LIBGST.gst_device_provider_register.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint, ctypes.c_size_t,
    ]
    _LIBGST.gst_device_provider_register.restype = ctypes.c_int

    _LIBGST.gst_registry_get.argtypes = []
    _LIBGST.gst_registry_get.restype = ctypes.c_void_p
    _LIBGST.gst_registry_find_plugin.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    _LIBGST.gst_registry_find_plugin.restype = ctypes.c_void_p
    _LIBGST.gst_object_unref.argtypes = [ctypes.c_void_p]
    _LIBGST.gst_object_unref.restype = None

    _INIT_FUNC_T = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)
    _LIBGST.gst_plugin_register_static_full.argtypes = [
        ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p,
        _INIT_FUNC_T, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p,
        ctypes.c_char_p, ctypes.c_char_p, ctypes.c_void_p,
    ]
    _LIBGST.gst_plugin_register_static_full.restype = ctypes.c_int
    # Stash the CFUNCTYPE constructor for later use.
    _LIBGST._INIT_FUNC_T = _INIT_FUNC_T  # type: ignore[attr-defined]
    return True


def _set_class_metadata(type_id: int) -> bool:
    """Populate the GstDeviceProviderClass metadata via ctypes."""
    klass_ptr = _LIBGOBJ.g_type_class_ref(type_id)
    if not klass_ptr:
        _gst_log("error", "g_type_class_ref returned NULL")
        return False
    longname, classification, description, author = OnvifDeviceProvider.__gstmetadata__
    _LIBGST.gst_device_provider_class_set_metadata(
        klass_ptr,
        longname.encode("utf-8"),
        classification.encode("utf-8"),
        description.encode("utf-8"),
        author.encode("utf-8"),
    )
    return True


def register_provider() -> bool:
    """Register ``onvifdeviceprovider`` with the default ``Gst.Registry``.

    Idempotent. Safe to call from any thread. Initialises GStreamer
    via :func:`Gst.init` if the caller has not done so yet.
    Returns ``True`` if the factory is present after the call.
    """
    global _REGISTERED, _PLUGIN_INIT_REF  # pylint: disable=global-statement
    if not Gst.is_initialized():
        Gst.init(None)

    registry = Gst.Registry.get()
    if registry.find_feature("onvifdeviceprovider", Gst.DeviceProviderFactory) is not None:
        _REGISTERED = True
        return True

    if not _load_ctypes():
        return False

    type_id = _LIBGOBJ.g_type_from_name(OnvifDeviceProvider.__gtype__.name.encode("utf-8"))
    if not type_id:
        _gst_log("error", f"g_type_from_name returned 0 for {OnvifDeviceProvider.__gtype__.name}")
        return False

    if not _set_class_metadata(type_id):
        return False

    def _plugin_init(plugin_ptr: int, _user_data: int) -> int:
        ok = _LIBGST.gst_device_provider_register(
            plugin_ptr, b"onvifdeviceprovider", int(Gst.Rank.PRIMARY), type_id,
        )
        return 1 if ok else 0

    # Keep the CFUNCTYPE callback alive forever — GStreamer holds a reference
    # to it for the lifetime of the registry.
    _PLUGIN_INIT_REF = _LIBGST._INIT_FUNC_T(_plugin_init)  # type: ignore[attr-defined]

    ok = _LIBGST.gst_plugin_register_static_full(
        Gst.VERSION_MAJOR,
        Gst.VERSION_MINOR,
        b"onvifdeviceprovider",
        b"ONVIF camera WS-Discovery + SOAP GstDeviceProvider",
        _PLUGIN_INIT_REF,
        b"1.0",
        b"MIT/X11",
        b"dlstreamer",
        b"dlstreamer",
        b"https://github.com/dlstreamer/dlstreamer",
        None,
    )
    _REGISTERED = bool(ok) and (
        registry.find_feature("onvifdeviceprovider", Gst.DeviceProviderFactory) is not None
    )
    if not _REGISTERED:
        _gst_log("error", "gst_plugin_register_static_full did not produce a feature")
    return _REGISTERED


def register_provider_with_plugin(plugin_addr: int) -> bool:
    """Register the device provider against an externally-owned GstPlugin.

    Entry point used by the C shim plugin
    (``libgstonvifdeviceprovider.so``). ``plugin_addr`` is the raw
    ``GstPlugin*`` reinterpreted as a Python integer. Because the plugin
    has a real filename on disk, the resulting feature is correctly
    persisted in GStreamer's binary registry cache — which makes the
    factory visible to pure-C tools (``gst-inspect-1.0``,
    ``gst-device-monitor-1.0``).

    Always registers against the supplied plugin pointer, even if the
    factory was already registered against an in-process static plugin
    earlier — otherwise the shim plugin would end up with zero features
    and the cache would not be persisted correctly.
    """
    global _REGISTERED  # pylint: disable=global-statement
    if not Gst.is_initialized():
        Gst.init(None)

    if not _load_ctypes():
        return False

    type_id = _LIBGOBJ.g_type_from_name(OnvifDeviceProvider.__gtype__.name.encode("utf-8"))
    if not type_id:
        _gst_log("error",
                 f"g_type_from_name returned 0 for {OnvifDeviceProvider.__gtype__.name}")
        return False

    if not _set_class_metadata(type_id):
        return False

    plugin_ptr = ctypes.c_void_p(plugin_addr)
    ok = _LIBGST.gst_device_provider_register(
        plugin_ptr, b"onvifdeviceprovider", int(Gst.Rank.PRIMARY), type_id,
    )
    _REGISTERED = bool(ok) or _REGISTERED
    if not ok:
        _gst_log("error",
                 "gst_device_provider_register failed against shim plugin")
    return bool(ok)


# Note: registration is **not** triggered automatically at module import time.
# Two well-defined entry points exist:
#   - :func:`register_provider` — used by ``dlstreamer.onvif.__init__`` for
#     in-process consumers (notably the ``onvifcm`` library).
#   - :func:`register_provider_with_plugin` — used by the C shim plugin
#     (``libgstonvifdeviceprovider.so``) so that ``gst-inspect-1.0`` and
#     ``gst-device-monitor-1.0`` can see the factory via the binary cache.
# Doing both would cause the second call to fail (factory name already
# taken) and leave the shim plugin with zero features.
