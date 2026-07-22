# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
WS-Discovery multicast probing for ONVIF-compliant devices.

This module implements a minimal client for the ONVIF profile of
WS-Discovery (SOAP-over-UDP multicast) and exposes both synchronous
and asynchronous generators that yield discovered cameras as
``{"hostname": str, "port": int}`` dictionaries.
"""
from __future__ import annotations

import asyncio
import json
import socket
import threading
import time
import uuid
from typing import AsyncIterator, Iterator, Optional
from urllib.parse import urlparse

from defusedxml import ElementTree as ET


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
# XML helpers
# ---------------------------------------------------------------------------


def extract_xaddrs(xml_string: str) -> Optional[str]:
    """Find ``XAddrs`` element in an ONVIF WS-Discovery ProbeMatch response."""

    try:
        root = ET.fromstring(xml_string)
        namespaces = {"wsdd": "http://schemas.xmlsoap.org/ws/2005/04/discovery"}
        xaddrs_element = root.find(".//wsdd:XAddrs", namespaces)
        if xaddrs_element is not None:
            return xaddrs_element.text
    except Exception as e:  # pylint: disable=broad-exception-caught
        print(f"Error parsing XML: {e}")
        return None
    return None


def parse_xaddrs_url(xaddrs: str) -> dict:
    """Parse an ``XAddrs`` URL string into components.

    ``XAddrs`` may contain multiple space-separated URLs; only the first is used.
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
    """Extract ``{"hostname", "port"}`` from an XAddrs URL string."""
    parsed = parse_xaddrs_url(xaddrs)
    if parsed["hostname"]:
        return {"hostname": parsed["hostname"], "port": parsed["port"] or 80}
    return None


# ---------------------------------------------------------------------------
# Synchronous discovery
# ---------------------------------------------------------------------------


def discover_onvif_cameras(verbose: bool = False) -> Iterator[dict]:
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


async def discover_onvif_cameras_async(
    verbose: bool = False,
) -> AsyncIterator[dict]:
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
