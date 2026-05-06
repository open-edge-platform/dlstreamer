"""
Utility module for ONVIF Camera Analytics Validation Pipeline.

Contains: ONVIF SOAP client, MQTT event listener, and cross-validation logic.
"""

import collections
import json
import logging
import queue
import subprocess
import time
from typing import Optional
from xml.etree import ElementTree

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
            capture_output=True, text=True, timeout=15)
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout
    except Exception as e:
        log.warning(f"SOAP {url}: {e}")
    return None


class ONVIFClient:
    """Lightweight ONVIF client for camera discovery via standard SOAP calls."""

    def __init__(self, ip: str, port: int = 80):
        base = f"http://{ip}:{port}/onvif"
        self.device_url = f"{base}/device_service"
        self.media_url = f"{base}/media_service"

    def get_device_info(self) -> dict:
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


# ═══════════════════════════════════════════════════════════════════════════
#  MQTT event parsing — camera-agnostic
# ═══════════════════════════════════════════════════════════════════════════

def _parse_json_event(payload: dict, topic: str) -> Optional[dict]:
    """Parse a JSON MQTT payload into a normalised event dict.

    Handles:
      - Standard ONVIF-style JSON with objectCount / classCounts
      - Common analytics JSON with data.ObjectType / data.IsMotion fields
    """
    # Already in standard format
    if "objectCount" in payload and payload["objectCount"] > 0:
        payload.setdefault("source", "mqtt_json")
        payload.setdefault("topic", topic)
        return payload

    # Look for data block (used by many cameras)
    data = payload.get("message", {}).get("data", payload.get("data", {}))
    if not data:
        return None

    timestamp = payload.get("timestamp", time.strftime(
        "%Y-%m-%dT%H:%M:%SZ", time.gmtime()))

    # Motion events
    is_motion = str(data.get("IsMotion", data.get("active", ""))).lower()
    if is_motion in ("1", "true"):
        return {
            "source": "mqtt_json", "topic": topic,
            "timestamp": timestamp,
            "objectCount": 1,
            "classCounts": {"Motion": 1},
            "objects": [{"type": "Motion", "confidence": 1.0}],
        }

    # Object analytics events
    obj_type = data.get("ObjectType", data.get("Type", ""))
    is_inside = str(data.get("IsInside", "true")).lower()
    if obj_type and is_inside in ("1", "true"):
        return {
            "source": "mqtt_json", "topic": topic,
            "timestamp": timestamp,
            "objectCount": 1,
            "classCounts": {obj_type: 1},
            "objects": [{"type": obj_type, "confidence": 0.0}],
        }

    return None


def _parse_onvif_xml_event(xml_str: str) -> Optional[dict]:
    """Parse ONVIF XML (MetadataStream or wsnt:Notify) from MQTT payload."""
    try:
        root = ElementTree.fromstring(xml_str)
    except ElementTree.ParseError:
        return None

    # tt:MetadataStream with tt:Object elements
    objects = []
    for obj in root.findall(".//tt:Object", NS):
        cls_el = obj.find(".//tt:Class/tt:Type", NS)
        obj_type = cls_el.text if cls_el is not None and cls_el.text else "Unknown"
        objects.append({"type": obj_type, "confidence": 0.0})

    if objects:
        class_counts = dict(collections.Counter(o["type"] for o in objects))
        return {
            "source": "onvif_xml",
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "objectCount": len(objects),
            "classCounts": class_counts,
            "objects": objects,
        }

    # wsnt:NotificationMessage
    events = []
    for notif in root.findall(".//wsnt:NotificationMessage", NS):
        msg_el = notif.find(".//tt:Message", NS)
        if msg_el is None:
            continue
        data = {}
        for item in msg_el.findall(".//tt:Data/tt:SimpleItem", NS):
            data[item.get("Name", "")] = item.get("Value", "")
        obj_type = data.get("ObjectType", data.get("Type", ""))
        is_inside = data.get("IsInside", "true").lower()
        if obj_type and is_inside in ("1", "true"):
            events.append({"type": obj_type, "confidence": 0.0})

    if events:
        class_counts = dict(collections.Counter(o["type"] for o in events))
        return {
            "source": "onvif_xml",
            "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "objectCount": len(events),
            "classCounts": class_counts,
            "objects": events,
        }

    return None


# ═══════════════════════════════════════════════════════════════════════════
#  MQTT Event Listener
# ═══════════════════════════════════════════════════════════════════════════

class MQTTEventListener:
    """Subscribes to MQTT topics and enqueues parsed camera events."""

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
        except Exception as e:
            log.error(f"MQTT connect failed: {e}")
            return False

    def stop(self):
        if self._client:
            self._client.loop_stop()
            self._client.disconnect()

    def _on_connect(self, client, userdata, flags, rc, properties=None):
        self.connected = True
        for topic in self.topics:
            client.subscribe(topic)
        log.info(f"MQTT subscribed: {', '.join(self.topics)}")

    def _on_disconnect(self, client, userdata, flags, rc, properties=None):
        self.connected = False

    def _on_message(self, client, userdata, msg):
        event = None
        raw_payload = msg.payload.decode("utf-8", errors="replace")

        # Try JSON first
        try:
            payload = json.loads(msg.payload)
            event = _parse_json_event(payload, msg.topic)
        except (json.JSONDecodeError, UnicodeDecodeError):
            pass

        # Fall back to ONVIF XML
        if event is None:
            try:
                event = _parse_onvif_xml_event(raw_payload)
            except Exception:
                pass

        if event:
            event["raw_mqtt"] = raw_payload
            event["raw_topic"] = msg.topic
            self.stats["events_received"] += 1
            try:
                self.event_queue.put_nowait(event)
            except queue.Full:
                self.stats["events_dropped"] += 1


# ═══════════════════════════════════════════════════════════════════════════
#  Cross-validation
# ═══════════════════════════════════════════════════════════════════════════

def cross_validate(camera_event: dict, vlm_detections: list) -> dict:
    """Compare camera-reported objects with VLM-extracted objects.

    Returns a dict with match/mismatch status and counts per class.
    """
    cam_types = collections.Counter(
        o.get("type", "") for o in camera_event.get("objects", []))
    vlm_types = collections.Counter(
        d.get("onvif_type", "") for d in vlm_detections)

    cam_count = camera_event.get("objectCount", 0)
    vlm_count = len(vlm_detections)

    missing = set(cam_types) - set(vlm_types)
    needs_update = bool(missing) or (cam_count > 0 and vlm_count == 0)

    return {
        "camera_objects": cam_count,
        "inference_objects": vlm_count,
        "camera_classes": dict(cam_types),
        "inference_classes": dict(vlm_types),
        "needs_update": needs_update,
    }
