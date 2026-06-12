"""
Utility module for ONVIF Camera Analytics Validation Pipeline.

Contains: ONVIF SOAP client and MQTT event listener.
"""
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import logging
import queue
import subprocess
import time
from typing import Optional

from defusedxml import ElementTree

import paho.mqtt.client as mqtt

log = logging.getLogger(__name__)

# ONVIF XML namespaces
NS = {
    "s": "http://www.w3.org/2003/05/soap-envelope",
    "tt": "http://www.onvif.org/ver10/schema",
    "trt": "http://www.onvif.org/ver10/media/wsdl",
    "tds": "http://www.onvif.org/ver10/device/wsdl",
    "tev": "http://www.onvif.org/ver10/events/wsdl",
    "wsnt": "http://docs.oasis-open.org/wsn/b-2",
}


# ═══════════════════════════════════════════════════════════════════════════
#  ONVIF SOAP client
# ═══════════════════════════════════════════════════════════════════════════

def _soap_request(url: str, body: str) -> Optional[str]:
    """Send a SOAP request and return the response body XML string."""
    envelope = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" '
        'xmlns:tds="http://www.onvif.org/ver10/device/wsdl" '
        'xmlns:trt="http://www.onvif.org/ver10/media/wsdl" '
        'xmlns:tt="http://www.onvif.org/ver10/schema" '
        'xmlns:tev="http://www.onvif.org/ver10/events/wsdl" '
        'xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2" '
        'xmlns:wsa="http://www.w3.org/2005/08/addressing">'
        f"<s:Body>{body}</s:Body></s:Envelope>"
    )
    try:
        result = subprocess.run(
            ["curl", "-s", "--max-time", "10", "-X", "POST", url,
             "-H", "Content-Type: application/soap+xml", "-d", envelope],
            capture_output=True, text=True, timeout=15, check=False)
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout
    except subprocess.SubprocessError as e:
        log.warning("SOAP %s: %s", url, e)
    return None


class ONVIFClient:
    """Lightweight ONVIF client for camera discovery via standard SOAP calls."""

    def __init__(self, ip: str, port: int = 80):
        base = f"http://{ip}:{port}/onvif"
        self.device_url = f"{base}/device_service"
        self.media_url = f"{base}/media_service"

    def get_device_info(self) -> dict:
        """Retrieve device information via ONVIF GetDeviceInformation."""
        resp = _soap_request(self.device_url, "<tds:GetDeviceInformation/>")
        if not resp:
            return {}
        try:
            root = ElementTree.fromstring(resp)
            info = root.find(".//tds:GetDeviceInformationResponse", NS)
            if info is not None:
                return {c.tag.split("}")[-1]: c.text for c in info}
        except ElementTree.ParseError:
            pass
        return {}

    def get_capabilities(self) -> dict:
        """Retrieve device capabilities via ONVIF GetCapabilities."""
        resp = _soap_request(
            self.device_url,
            '<tds:GetCapabilities>'
            '<tds:Category>All</tds:Category>'
            '</tds:GetCapabilities>')
        if not resp:
            return {}
        caps = {}
        try:
            root = ElementTree.fromstring(resp)
            el = root.find(".//tt:Capabilities", NS)
            if el is not None:
                for child in el:
                    tag = child.tag.split("}")[-1]
                    xaddr = child.get("XAddr", "")
                    if not xaddr:
                        xel = child.find("tt:XAddr", NS)
                        if xel is not None and xel.text:
                            xaddr = xel.text
                    caps[tag] = xaddr
                    if tag == "Media" and xaddr:
                        self.media_url = xaddr
        except ElementTree.ParseError:
            pass
        return caps

    def get_profiles(self) -> list:
        """Retrieve media profiles via ONVIF GetProfiles."""
        resp = _soap_request(self.media_url, "<trt:GetProfiles/>")
        if not resp:
            return []
        profiles = []
        try:
            root = ElementTree.fromstring(resp)
            for prof in root.findall(".//trt:Profiles", NS):
                token = prof.get("token", "")
                name_el = prof.find("tt:Name", NS)
                ve = prof.find(".//tt:VideoEncoderConfiguration", NS)
                vid = {}
                if ve is not None:
                    enc = ve.find("tt:Encoding", NS)
                    res = ve.find("tt:Resolution", NS)
                    vid = {
                        "encoding": enc.text if enc is not None else "",
                        "width": (res.find("tt:Width", NS).text
                                  if res is not None else ""),
                        "height": (res.find("tt:Height", NS).text
                                   if res is not None else ""),
                    }
                profiles.append({
                    "token": token,
                    "name": name_el.text if name_el is not None else "",
                    "video": vid,
                })
        except ElementTree.ParseError:
            pass
        return profiles

    def get_stream_uri(self, profile_token: str) -> str:
        """Retrieve the RTSP stream URI for the given profile token."""
        body = (
            "<trt:GetStreamUri><trt:StreamSetup>"
            "<tt:Stream>RTP-Unicast</tt:Stream>"
            "<tt:Transport><tt:Protocol>RTSP</tt:Protocol></tt:Transport>"
            "</trt:StreamSetup>"
            f"<trt:ProfileToken>{profile_token}</trt:ProfileToken>"
            "</trt:GetStreamUri>")
        resp = _soap_request(self.media_url, body)
        if not resp:
            return ""
        try:
            root = ElementTree.fromstring(resp)
            uri = root.find(".//tt:Uri", NS)
            if uri is not None and uri.text:
                return uri.text.replace("&amp;", "&")
        except ElementTree.ParseError:
            pass
        return ""

    def get_scopes(self) -> list:
        """Retrieve ONVIF device scopes."""
        resp = _soap_request(self.device_url, "<tds:GetScopes/>")
        if not resp:
            return []
        scopes = []
        try:
            root = ElementTree.fromstring(resp)
            for scope in root.findall(".//tds:Scopes", NS):
                item = scope.find("tt:ScopeItem", NS)
                if item is not None and item.text:
                    scopes.append(item.text)
        except ElementTree.ParseError:
            pass
        return scopes

    def get_event_topics(self) -> list:
        """Discover available analytics event topics via ONVIF Events service."""
        caps = self.get_capabilities()
        events_url = caps.get("Events", "")
        if not events_url:
            events_url = self.device_url.replace(
                "/device_service", "/event_service")
        resp = _soap_request(events_url, "<tev:GetEventProperties/>")
        if not resp:
            return []
        topics = []
        try:
            root = ElementTree.fromstring(resp)
            for topic_set in root.findall(".//{http://docs.oasis-open.org/wsn/t-1}TopicSet"):
                for el in topic_set.iter():
                    tag = el.tag.split("}")[-1] if "}" in el.tag else el.tag
                    if tag and el.get("{http://docs.oasis-open.org/wsn/t-1}topic") == "true":
                        topics.append(tag)
            # Also look for topic elements directly
            for topic_el in root.findall(".//{http://docs.oasis-open.org/wsn/t-1}Topic"):
                dialect = topic_el.get("Dialect", "")
                if topic_el.text and "Concrete" in dialect:
                    topics.append(topic_el.text.strip())
        except ElementTree.ParseError:
            pass
        return topics


# ═══════════════════════════════════════════════════════════════════════════
#  MQTT Event Listener
# ═══════════════════════════════════════════════════════════════════════════

class MQTTEventListener:
    """Subscribes to MQTT topics and enqueues raw camera events.

    All MQTT messages are passed through as-is — the VLM interprets
    the raw payload directly, making this truly camera-agnostic.
    """

    def __init__(self, broker: str, port: int,
                 topics: Optional[list] = None):
        self.broker = broker
        self.port = port
        self.topics = topics or ["onvif/analytics/#"]
        self.event_queue: queue.Queue = queue.Queue(maxsize=100)
        self.connected = False
        self.stats = {"events_received": 0, "events_dropped": 0}
        self._client = None

    def start(self) -> bool:
        """Connect to the MQTT broker and start listening."""
        self._client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=f"validator-{int(time.time())}",
        )
        self._client.on_connect = self._on_connect
        self._client.on_message = self._on_message
        self._client.on_disconnect = self._on_disconnect
        try:
            self._client.connect(self.broker, self.port, 60)
            self._client.loop_start()
            return True
        except OSError as e:
            log.error("MQTT connect failed: %s", e)
            return False

    def stop(self):
        """Disconnect from the MQTT broker."""
        if self._client:
            self._client.loop_stop()
            self._client.disconnect()

    def _on_connect(  # pylint: disable=too-many-arguments,too-many-positional-arguments
        self, _client, _userdata, _flags, _rc, _properties=None,
    ):
        """Handle MQTT connection event."""
        self.connected = True
        for topic in self.topics:
            self._client.subscribe(topic)
        log.info("MQTT subscribed: %s", ", ".join(self.topics))

    def _on_disconnect(  # pylint: disable=too-many-arguments,too-many-positional-arguments
        self, _client, _userdata, _flags, _rc, _properties=None,
    ):
        """Handle MQTT disconnection event."""
        self.connected = False

    def _on_message(self, _client, _userdata, msg):
        """Pass any non-empty MQTT message through for VLM interpretation."""
        raw_payload = msg.payload.decode("utf-8", errors="replace").strip()
        if not raw_payload:
            return

        event = {
            "source": "mqtt",
            "topic": msg.topic,
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "raw_payload": raw_payload,
        }
        self.stats["events_received"] += 1
        try:
            self.event_queue.put_nowait(event)
        except queue.Full:
            self.stats["events_dropped"] += 1

