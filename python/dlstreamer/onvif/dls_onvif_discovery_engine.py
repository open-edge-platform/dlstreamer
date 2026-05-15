"""
ONVIF Camera Discovery Engine.

Unified module providing:
- WS-Discovery multicast probing for ONVIF cameras
- XML parsing of ProbeMatch responses
- ONVIF media profile retrieval (video/audio encoder, PTZ, RTSP URIs)
- Async discovery orchestrator with camera registry and pipeline management
"""

# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import asyncio
import json
import socket
import threading
import time
import uuid
import xml.etree.ElementTree as ET
from typing import Any, AsyncIterator, Iterator, List, Optional
from urllib.parse import urlparse

import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst  # pylint: disable=wrong-import-position,no-name-in-module

from onvif import ONVIFCamera  # pylint: disable=wrong-import-position,import-error

from .dls_onvif_data import ONVIFProfile
from . import dls_onvif_discovery_thread as dls_disc_thread
from .dls_onvif_camera_entry import (
    CameraStatus,
    DlsOnvifCameraEntry,
    DlsOnvifCameraRegistry,
)
from .misc import print_cameras
from . import dls_onvif_config_manager

# ---------------------------------------------------------------------------
# WS-Discovery constants
# ---------------------------------------------------------------------------

_MCAST_GRP = "239.255.255.250"
_MCAST_PORT = 3702
_SOCKET_TIMEOUT = 5

_PROBE_TEMPLATE = """<?xml version="1.0" encoding="UTF-8"?>
<soap:Envelope xmlns:soap="http://www.w3.org/2003/05/soap-envelope"
               xmlns:wsa="http://schemas.xmlsoap.org/ws/2004/08/addressing"
               xmlns:tns="http://schemas.xmlsoap.org/ws/2005/04/discovery"
               xmlns:dn="http://www.onvif.org/ver10/network/wsdl">
    <soap:Header>
        <wsa:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</wsa:Action>
        <wsa:MessageID>uuid:{message_id}</wsa:MessageID>
        <wsa:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</wsa:To>
    </soap:Header>
    <soap:Body>
        <tns:Probe>
            <tns:Types>dn:NetworkVideoTransmitter</tns:Types>
        </tns:Probe>
    </soap:Body>
</soap:Envelope>"""


# ---------------------------------------------------------------------------
# WS-Discovery XML helpers
# ---------------------------------------------------------------------------


def extract_xaddrs(xml_string):
    """Find XAddrs in ONVIF discovery response"""

    try:
        # Parse XML
        root = ET.fromstring(xml_string)

        # Namespace for wsdd
        namespaces = {"wsdd": "http://schemas.xmlsoap.org/ws/2005/04/discovery"}

        # Find XAddrs
        xaddrs_element = root.find(".//wsdd:XAddrs", namespaces)

        if xaddrs_element is not None:
            return xaddrs_element.text

    except Exception as e:  # pylint: disable=broad-exception-caught
        print(f"Error parsing XML: {e}")
        return None
    return None


def parse_xaddrs_url(xaddrs):
    """Parse XAddrs URL into components.

    XAddrs may contain multiple space-separated URLs; only the first is used.
    """

    first_url = xaddrs.split()[0] if xaddrs else xaddrs
    parsed = urlparse(first_url)

    return {
        "full_url": first_url,
        "scheme": parsed.scheme,
        "hostname": parsed.hostname,
        "port": parsed.port,
        "path": parsed.path,
        "base_url": f"{parsed.scheme}://{parsed.netloc}",
    }


def _parse_camera_from_xaddrs(xaddrs: str) -> Optional[dict]:
    """Extract hostname and port from an XAddrs URL string."""
    parsed = parse_xaddrs_url(xaddrs)
    if parsed["hostname"]:
        return {"hostname": parsed["hostname"], "port": parsed["port"] or 80}
    return None


# ---------------------------------------------------------------------------
# Synchronous discovery
# ---------------------------------------------------------------------------


def discover_onvif_cameras(
    verbose: bool = False,
) -> Iterator[dict]:
    """Find ONVIF cameras in the local network using WS-Discovery.

    Yields each camera dict as soon as it is discovered, enabling
    incremental processing by callers.
    """

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.settimeout(_SOCKET_TIMEOUT)

    try:
        count = 0
        probe = _PROBE_TEMPLATE.format(message_id=uuid.uuid4())
        sock.sendto(probe.encode(), (_MCAST_GRP, _MCAST_PORT))

        start_time = time.time()
        while time.time() - start_time < _SOCKET_TIMEOUT:
            remaining_time = _SOCKET_TIMEOUT - (time.time() - start_time)
            if remaining_time <= 0:
                break
            sock.settimeout(remaining_time)
            try:
                data, addr = sock.recvfrom(65535)
                if verbose:
                    print(f"Response from {addr}")

                response = data.decode("utf-8", errors="ignore")
                xaddrs = extract_xaddrs(response)
                if not xaddrs:
                    continue

                camera = _parse_camera_from_xaddrs(xaddrs)
                if camera:
                    count += 1
                    if verbose:
                        print(json.dumps(camera))
                    yield camera

            except socket.timeout:
                break

        if verbose:
            print(f"Discovery complete. Found {count} camera(s).")
    finally:
        sock.close()


# ---------------------------------------------------------------------------
# Async discovery generator
# ---------------------------------------------------------------------------


async def discover_onvif_cameras_async(verbose: bool = False) -> AsyncIterator[dict]:
    """Find ONVIF cameras in the local network using WS-Discovery, yielding each camera as found.

    Runs the blocking socket I/O in a daemon thread and publishes each
    discovered camera incrementally via an ``asyncio.Queue`` so that
    the caller can start processing cameras before the full discovery
    pass completes.

    Usage::

        async for camera in discover_onvif_cameras_async():
            print(camera)

    Yields:
        dict: ``{"hostname": str, "port": int}`` for every discovered camera.
    """
    loop = asyncio.get_running_loop()
    queue: asyncio.Queue[dict | None] = asyncio.Queue()

    def worker():
        try:
            for camera in discover_onvif_cameras(verbose):
                loop.call_soon_threadsafe(queue.put_nowait, camera)
        except Exception as exc:  # pylint: disable=broad-exception-caught
            print(f"[ERROR] Discovery worker failed: {exc}")
        finally:
            loop.call_soon_threadsafe(queue.put_nowait, None)

    threading.Thread(target=worker, daemon=True).start()

    while True:
        item = await queue.get()
        if item is None:
            break
        yield item


# ---------------------------------------------------------------------------
# Async discovery orchestrator
# ---------------------------------------------------------------------------


class DlsOnvifDiscoveryEngine:  # pylint: disable=too-many-instance-attributes
    """Asynchronous utility class for ONVIF camera discovery and profile retrieval."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.registry = DlsOnvifCameraRegistry()
        self.is_discovery_running: bool = False
        self.refresh_rate: int = 60
        self.username = None
        self.password = None
        self.verbose: bool = False
        self.config_manager = None

        Gst.init(None)

    def init_discovery(self, config: dict[str, Any]) -> bool:
        """Initialize the ONVIF discovery process asynchronously."""

        self.refresh_rate = int(config.get("refresh_rate", self.refresh_rate))
        self.username = config.get("user", self.username)
        self.password = config.get("password", self.password)
        self.verbose = bool(config.get("verbose", self.verbose))
        self.config_manager = dls_onvif_config_manager.DlsOnvifConfigManager(
            config.get("config_file", "config.json")
        )
        # Config file verbose overrides if CLI didn't set it
        if not self.verbose:
            self.verbose = self.config_manager.verbose
        return True

    async def _countdown_to_next_cycle(self) -> None:
        """Display a one-line countdown before the next discovery cycle."""

        if self.refresh_rate <= 0:
            return

        try:
            for remaining_seconds in range(self.refresh_rate, 0, -1):
                print(
                    f"\rNext ONVIF discovery cycle "
                    f"in \033[44m\033[97m {remaining_seconds:2d}s \033[0m",
                    end="",
                    flush=True,
                )
                await asyncio.sleep(1)
        finally:
            print(
                "\r" + "ONVIF discovery cycle started ...    " + "\r",
                end="",
                flush=True,
            )

    async def discover_cameras_iter(self) -> AsyncIterator[dict[str, Any]]:
        """Discover ONVIF cameras asynchronously and yield them as they are found."""

        self.is_discovery_running = True

        while self.is_discovery_running:

            self.config_manager.refresh_cameras()  # Refresh camera list from config file

            current_camera_ids: set[str] = set()

            async for camera in discover_onvif_cameras_async():
                camera_id = self._camera_id_from_dict(camera)
                current_camera_ids.add(camera_id)

                # Update existing entry or create new one
                existing = self.registry.get(camera_id)
                if existing:
                    existing.touch()
                else:
                    entry = DlsOnvifCameraEntry.from_discovery_dict(
                        camera, self.username or "", self.password or ""
                    )
                    await self._create_pipelines_for_entry(entry)
                    self.registry.add(entry)

                yield camera

            # Remove cameras no longer seen
            self._remove_stale_cameras(current_camera_ids)

            # End this discovery loop if the flag is set to False,
            # otherwise wait for the next refresh cycle
            if not self.is_discovery_running:
                break

            try:
                await self._countdown_to_next_cycle()

            except asyncio.CancelledError:
                self.is_discovery_running = False
                break

    def get_cameras(self) -> list[dict[str, Any]]:
        """Get the list of currently discovered cameras."""

        return [entry.raw_discovery for entry in self.registry.all_entries()]

    async def release_resources_async(self) -> None:
        """Release resources held by the discovery engine."""

        self.is_discovery_running = False

        stop_errors = self.registry.stop_all()

        if stop_errors:
            raise RuntimeError("; ".join(stop_errors))

    def release_resources(self) -> None:
        """Release any resources held by the discovery utility.
        For this async version, there may be no resources to release, but this method
        is provided for interface consistency."""

        try:
            asyncio.get_running_loop()
        except RuntimeError:
            asyncio.run(self.release_resources_async())
            return

        raise RuntimeError(
            "release_resources() cannot run inside an active event loop. "
            "Use: await release_resources_async()."
        )

    def camera_profiles(
        self, client
    ):  # pylint: disable=too-many-statements, too-many-locals, too-many-branches
        """Query an ONVIF camera for its available media profiles.

        Extracts detailed configuration information including video encoder
        settings, audio configurations, PTZ capabilities, and RTSP streaming URIs.

        Args:
            client: An ONVIF client instance used to communicate with the camera.

        Returns:
            List[ONVIFProfile]: A list of ONVIFProfile objects containing the
                extracted profile information.
        """
        verbose = self.verbose

        media_service = client.create_media_service()

        profiles = media_service.GetProfiles()

        onvif_profiles: List[ONVIFProfile] = []

        for i, profile in enumerate(profiles, 1):
            onvif_profile: ONVIFProfile = ONVIFProfile()
            onvif_profile.name = profile.Name
            onvif_profile.token = profile.token
            if verbose:
                print(f"  Profile {i}:")
                print(f"    Name: {onvif_profile.name}")
                print(f"    Token: {onvif_profile.token}")

            # Fixed profile indicator
            if hasattr(profile, "fixed") and profile.fixed is not None:
                onvif_profile.fixed = profile.fixed

            # Video Source Configuration
            if (
                hasattr(profile, "VideoSourceConfiguration")
                and profile.VideoSourceConfiguration
            ):
                vsc = profile.VideoSourceConfiguration
                onvif_profile.vsc_name = vsc.Name
                onvif_profile.vsc_token = vsc.token
                onvif_profile.vsc_source_token = vsc.SourceToken
                if hasattr(vsc, "Bounds") and vsc.Bounds:
                    onvif_profile.vsc_bounds = {
                        "x": vsc.Bounds.x,
                        "y": vsc.Bounds.y,
                        "width": vsc.Bounds.width,
                        "height": vsc.Bounds.height,
                    }

            # Video Encoder Configuration
            if (
                hasattr(profile, "VideoEncoderConfiguration")
                and profile.VideoEncoderConfiguration
            ):
                vec = profile.VideoEncoderConfiguration
                onvif_profile.vec_name = vec.Name
                onvif_profile.vec_token = vec.token
                onvif_profile.vec_encoding = vec.Encoding
                if verbose:
                    print("    Video Encoder:")
                    print(f"      Name: {vec.Name}")
                    print(f"      Token: {vec.token}")
                    print(f"      Encoding: {vec.Encoding}")
                if hasattr(vec, "Resolution") and vec.Resolution:
                    onvif_profile.vec_resolution = {
                        "width": vec.Resolution.Width,
                        "height": vec.Resolution.Height,
                    }
                    if verbose:
                        print(
                            f"      Resolution: {vec.Resolution.Width}x{vec.Resolution.Height}"
                        )
                if hasattr(vec, "Quality"):
                    onvif_profile.vec_quality = vec.Quality
                    if verbose:
                        print(f"      Quality: {vec.Quality}")
                if hasattr(vec, "RateControl") and vec.RateControl:
                    onvif_profile.vec_framerate_limit = vec.RateControl.FrameRateLimit
                    onvif_profile.vec_bitrate_limit = vec.RateControl.BitrateLimit
                    if verbose:
                        print(
                            f"      FrameRate Limit: {vec.RateControl.FrameRateLimit}"
                        )
                        print(f"      Bitrate Limit: {vec.RateControl.BitrateLimit}")
                    if hasattr(vec.RateControl, "EncodingInterval"):
                        onvif_profile.vec_encoding_interval = (
                            vec.RateControl.EncodingInterval
                        )
                        if verbose:
                            print(
                                f"      Encoding Interval: "
                                f"{vec.RateControl.EncodingInterval}"
                            )
                if hasattr(vec, "H264") and vec.H264:
                    onvif_profile.vec_h264_profile = vec.H264.H264Profile
                    onvif_profile.vec_h264_gop_length = vec.H264.GovLength
                    if verbose:
                        print(f"      H264 Profile: {vec.H264.H264Profile}")
                        print(f"      GOP Size: {vec.H264.GovLength}")
                elif hasattr(vec, "MPEG4") and vec.MPEG4:
                    onvif_profile.vec_mpeg4_profile = vec.MPEG4.Mpeg4Profile
                    onvif_profile.vec_mpeg4_gop_length = vec.MPEG4.GovLength
                    if verbose:
                        print(f"      MPEG4 Profile: {vec.MPEG4.Mpeg4Profile}")
                        print(f"      GOP Size: {vec.MPEG4.GovLength}")

            # Audio Source Configuration
            if (
                hasattr(profile, "AudioSourceConfiguration")
                and profile.AudioSourceConfiguration
            ):
                asc = profile.AudioSourceConfiguration
                onvif_profile.asc_name = asc.Name
                onvif_profile.asc_token = asc.token
                onvif_profile.asc_source_token = asc.SourceToken
                if verbose:
                    print(f"      Name: {asc.Name}")
                    print(f"      Token: {asc.token}")
                    print(f"      SourceToken: {asc.SourceToken}")

            # Audio Encoder Configuration
            if (
                hasattr(profile, "AudioEncoderConfiguration")
                and profile.AudioEncoderConfiguration
            ):
                aec = profile.AudioEncoderConfiguration
                onvif_profile.aec_name = aec.Name
                onvif_profile.aec_token = aec.token
                onvif_profile.aec_encoding = aec.Encoding
                if verbose:
                    print("    Audio Encoder:")
                    print(f"      Name: {aec.Name}")
                    print(f"      Token: {aec.token}")
                    print(f"      Encoding: {aec.Encoding}")
                if hasattr(aec, "Bitrate"):
                    onvif_profile.aec_bitrate = aec.Bitrate
                    if verbose:
                        print(f"      Bitrate: {aec.Bitrate}")
                if hasattr(aec, "SampleRate"):
                    onvif_profile.aec_sample_rate = aec.SampleRate
                    if verbose:
                        print(f"      SampleRate: {aec.SampleRate}")

            # PTZ Configuration
            if hasattr(profile, "PTZConfiguration") and profile.PTZConfiguration:
                ptz = profile.PTZConfiguration
                onvif_profile.ptz_name = ptz.Name
                onvif_profile.ptz_token = ptz.token
                onvif_profile.ptz_node_token = ptz.NodeToken
                if verbose:
                    print("    PTZ:")
                    print(f"      Name: {ptz.Name}")
                    print(f"      Token: {ptz.token}")
                    print(f"      NodeToken: {ptz.NodeToken}")

            # Get Stream URI for this profile
            try:
                stream_setup = {
                    "Stream": "RTP-Unicast",
                    "Transport": {"Protocol": "RTSP"},
                }
                rtsp_uri = media_service.GetStreamUri(
                    {"StreamSetup": stream_setup, "ProfileToken": profile.token}
                )
                onvif_profile.rtsp_url = rtsp_uri.Uri
                if verbose:
                    print(f"        Stream URI: {rtsp_uri.Uri}")
            except (
                AttributeError,
                KeyError,
                TimeoutError,
                ConnectionError,
            ) as e:
                print(
                    f"[WARN] Failed to get Stream URI for profile "
                    f"'{profile.Name}': {type(e).__name__} - {e}"
                )
            except Exception as e:  # pylint: disable=broad-exception-caught
                print(
                    f"[WARN] Failed to get Stream URI for profile "
                    f"'{profile.Name}': {e}"
                )
            if verbose:
                print("  ----------------------- ")

            onvif_profiles.append(onvif_profile)

        return onvif_profiles

    @staticmethod
    def _camera_id_from_dict(camera: dict[str, Any]) -> str:
        """Derive a camera_id from a raw WS-Discovery dict."""
        hostname = camera.get("hostname") or camera.get("ip_address", "")
        port = camera.get("port", 80)
        return f"{hostname}:{port}"

    def _remove_stale_cameras(self, current_ids: set[str]) -> None:
        """Stop pipelines and remove registry entries for cameras no longer seen."""
        stale_ids = self.registry.camera_ids() - current_ids
        if not stale_ids:
            return

        removed_dicts = []
        for camera_id in stale_ids:
            entry = self.registry.remove(camera_id)
            if entry:
                errors = entry.stop_all_pipelines()
                for err in errors:
                    print(f"[ERROR] Failed to stop pipeline for '{camera_id}': {err}")
                entry.mark_removed()
                removed_dicts.append(entry.raw_discovery)

        if removed_dicts:
            print_cameras("Removed cameras", removed_dicts)

    def _load_profiles_sync(self, entry: DlsOnvifCameraEntry) -> list:
        """Connect to camera via ONVIF and retrieve media profiles (blocking)."""
        camera_obj = ONVIFCamera(
            entry.hostname,
            entry.port,
            self.username,
            self.password,
        )
        return self.camera_profiles(camera_obj)

    async def _create_pipelines_for_entry(self, entry: DlsOnvifCameraEntry) -> None:
        """Connect to a camera, retrieve profiles, create and start pipelines.

        Populates ``entry.profiles`` and ``entry.pipelines`` in-place,
        then schedules async start tasks for each pipeline.
        The blocking ONVIF interaction is offloaded to a worker thread
        to keep the event loop responsive.
        """
        camera_ip = entry.hostname
        camera_port = entry.port

        pipeline_definition = self.config_manager.get_pipeline_definition_by_ip_port(
            camera_ip, camera_port
        )

        if not pipeline_definition:
            print(
                f"[WARN] No pipeline definition in config for "
                f"'{camera_ip}:{camera_port}', skipping."
            )
            return

        entry.status = CameraStatus.CONNECTING

        # Connect to the camera via ONVIF and get profiles in a worker thread
        try:
            profiles = await asyncio.to_thread(self._load_profiles_sync, entry)
        except Exception as e:  # pylint: disable=broad-exception-caught
            entry.mark_error(
                f"Failed to connect to ONVIFCamera '{camera_ip}:{camera_port}': {e}"
            )
            print(f"[ERROR] {entry.error_message}")
            return

        if not profiles:
            entry.mark_error(f"No profiles found for camera '{camera_ip}'")
            print(f"[WARN] {entry.error_message}, skipping.")
            return

        entry.profiles = profiles
        if self.verbose:
            print_cameras(
                f"Profiles for {camera_ip}",
                [
                    {
                        "ip": camera_ip,
                        "port": camera_port,
                        "name": p.name,
                        "token": p.token,
                        "fixed": p.fixed,
                        "encoding": p.vec_encoding,
                        "resolution": (
                            f"{p.vec_resolution.get('width', '?')}x"
                            f"{p.vec_resolution.get('height', '?')}"
                            if p.vec_resolution
                            else "-"
                        ),
                        "quality": p.vec_quality or "-",
                        "fps_limit": getattr(p, "vec_framerate_limit", "-"),
                        "bitrate_limit": getattr(p, "vec_bitrate_limit", "-"),
                        "enc_interval": getattr(p, "vec_encoding_interval", "-"),
                        "h264_profile": getattr(p, "vec_h264_profile", "-"),
                        "h264_gop": getattr(p, "vec_h264_gop_length", "-"),
                        "mpeg4_profile": getattr(p, "vec_mpeg4_profile", "-"),
                        "mpeg4_gop": getattr(p, "vec_mpeg4_gop_length", "-"),
                        "vsc_name": p.vsc_name or "-",
                        "vsc_bounds": (
                            f"{p.vsc_bounds.get('width', '?')}x"
                            f"{p.vsc_bounds.get('height', '?')}"
                            if p.vsc_bounds
                            else "-"
                        ),
                        "aec_encoding": p.aec_encoding or "-",
                        "aec_bitrate": p.aec_bitrate or "-",
                        "aec_sample_rate": p.aec_sample_rate or "-",
                        "ptz": p.ptz_name or "-",
                        "rtsp_url": p.rtsp_url or "-",
                    }
                    for p in profiles
                ],
            )

        for i, profile in enumerate(profiles, 1):
            rtsp_url = profile.rtsp_url
            if not rtsp_url:
                print(
                    f"[WARN] No RTSP URL for profile "
                    f"'{profile.name}' on {camera_ip}, skipping."
                )
                continue

            full_pipeline = f'rtspsrc location="{rtsp_url}" {pipeline_definition}'

            pipeline = dls_disc_thread.DlsLaunchedPipeline(
                full_pipeline,
                f"{camera_ip} - {profile.name}",
            )
            pipeline.ip_address = camera_ip
            pipeline.profile = profile.name

            entry.add_pipeline(pipeline)

            # Start pipelines sequentially to avoid concurrent X11/GStreamer
            # state transitions which cause heap corruption (SIGABRT) in
            # ximagesink when multiple threads call gst_x_image_sink_xcontext_get
            # simultaneously.
            try:
                await asyncio.to_thread(pipeline.start)
            except Exception as e:  # pylint: disable=broad-exception-caught
                print(
                    f"[ERROR] Pipeline start failed for {camera_ip}/{profile.name}: {e}"
                )

            print(f"Created pipeline [{i}]: IP={camera_ip}, Profile={profile.name}")

        if entry.active_pipeline_count > 0:
            entry.mark_streaming()
        else:
            entry.mark_error(f"No streamable profiles for '{camera_ip}'")
