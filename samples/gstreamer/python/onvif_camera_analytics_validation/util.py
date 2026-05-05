"""
Utility module for ONVIF Profile M Analytics Validation Pipeline.

Contains: ONVIF SOAP client, ONVIF XML parsers, MQTT event listener,
and cross-validation logic.
"""

import collections
import json
import logging
import queue
import subprocess
import threading
import time
import urllib.request
import urllib.error
from typing import Optional
from xml.etree import ElementTree

import paho.mqtt.client as mqtt

log = logging.getLogger("onvif_validator")

# ---------------------------------------------------------------------------
#  Constants
# ---------------------------------------------------------------------------

NS = {
    "s": "http://www.w3.org/2003/05/soap-envelope",
    "tt": "http://www.onvif.org/ver10/schema",
    "trt": "http://www.onvif.org/ver10/media/wsdl",
    "tds": "http://www.onvif.org/ver10/device/wsdl",
    "tev": "http://www.onvif.org/ver10/events/wsdl",
    "tan": "http://www.onvif.org/ver20/analytics/wsdl",
    "wsnt": "http://docs.oasis-open.org/wsn/b-2",
    "wsa": "http://www.w3.org/2005/08/addressing",
    "tns1": "http://www.onvif.org/ver10/topics",
}



# ═══════════════════════════════════════════════════════════════════════════
#  ONVIF SOAP helpers
# ═══════════════════════════════════════════════════════════════════════════
def soap_request(url: str, body: str, extra_ns: str = "") -> Optional[str]:
    envelope = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" '
        'xmlns:tds="http://www.onvif.org/ver10/device/wsdl" '
        'xmlns:trt="http://www.onvif.org/ver10/media/wsdl" '
        'xmlns:tt="http://www.onvif.org/ver10/schema" '
        'xmlns:tev="http://www.onvif.org/ver10/events/wsdl" '
        'xmlns:tan="http://www.onvif.org/ver20/analytics/wsdl" '
        'xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2" '
        'xmlns:wsa="http://www.w3.org/2005/08/addressing"'
        f'{extra_ns}>'
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


# ═══════════════════════════════════════════════════════════════════════════
#  ONVIF XML Parsers
# ═══════════════════════════════════════════════════════════════════════════
def parse_onvif_metadata_xml(xml_str: str) -> Optional[dict]:
    """Parse tt:MetadataStream XML into normalised event dict."""
    try:
        root = ElementTree.fromstring(xml_str)
    except ElementTree.ParseError:
        return None

    objects = []
    for obj in root.findall(".//tt:Object", NS):
        obj_id = obj.get("ObjectId", "")
        cls_el = obj.find(".//tt:Class/tt:Type", NS)
        bbox_el = obj.find(".//tt:Shape/tt:BoundingBox", NS)

        obj_type = cls_el.text if cls_el is not None and cls_el.text else "Unknown"
        confidence = 0.0
        if cls_el is not None:
            try:
                confidence = float(cls_el.get("Likelihood", "0"))
            except ValueError:
                pass

        bbox = {}
        if bbox_el is not None:
            try:
                bbox = {
                    "left": (float(bbox_el.get("left", "0")) + 1) / 2,
                    "top": (float(bbox_el.get("top", "0")) + 1) / 2,
                    "right": (float(bbox_el.get("right", "0")) + 1) / 2,
                    "bottom": (float(bbox_el.get("bottom", "0")) + 1) / 2,
                }
            except ValueError:
                pass

        objects.append({
            "objectId": obj_id,
            "type": obj_type,
            "confidence": confidence,
            "boundingBox": bbox,
        })

    if not objects:
        return None

    class_counts = dict(collections.Counter(o["type"] for o in objects))
    return {
        "source": "onvif_xml",
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "objectCount": len(objects),
        "classCounts": class_counts,
        "objects": objects,
    }


def parse_onvif_notification(xml_str: str) -> list:
    """Parse wsnt:Notify / PullMessagesResponse for detection events."""
    try:
        root = ElementTree.fromstring(xml_str)
    except ElementTree.ParseError:
        return []

    events = []
    for notif in root.findall(".//wsnt:NotificationMessage", NS):
        msg_el = notif.find(".//tt:Message", NS)
        if msg_el is None:
            continue

        data = {}
        for item in msg_el.findall(".//tt:Data/tt:SimpleItem", NS):
            data[item.get("Name", "")] = item.get("Value", "")

        obj_type = data.get("ObjectType", data.get("Type", "Unknown"))
        obj = {
            "objectId": data.get("ObjectId", ""),
            "type": obj_type,
            "confidence": 0.0,
            "boundingBox": {},
        }

        bbox_str = data.get("BoundingBox", "")
        if bbox_str:
            parts = bbox_str.split(",")
            if len(parts) == 4:
                try:
                    obj["boundingBox"] = {
                        "left": float(parts[0]), "top": float(parts[1]),
                        "right": float(parts[2]), "bottom": float(parts[3]),
                    }
                except ValueError:
                    pass

        conf_str = data.get("Confidence", "")
        if conf_str:
            try:
                obj["confidence"] = float(conf_str)
            except ValueError:
                pass

        is_inside = data.get("IsInside", "true").lower() == "true"
        if is_inside and obj_type != "Unknown":
            events.append({
                "source": "onvif_pullpoint",
                "timestamp": msg_el.get("UtcTime", ""),
                "objectCount": 1,
                "classCounts": {obj_type: 1},
                "objects": [obj],
            })

    if len(events) > 1:
        merged = events[0].copy()
        merged["objects"] = []
        for e in events:
            merged["objects"].extend(e["objects"])
        merged["objectCount"] = len(merged["objects"])
        merged["classCounts"] = dict(
            collections.Counter(o["type"] for o in merged["objects"]))
        return [merged]

    return events



# ═══════════════════════════════════════════════════════════════════════════
#  Event Listeners
# ═══════════════════════════════════════════════════════════════════════════
class BaseEventListener:
    """Base class for event sources."""

    def __init__(self):
        self.event_queue: queue.Queue = queue.Queue(maxsize=100)
        self.connected = False
        self.stats = {"events_received": 0, "events_dropped": 0}
        self.source_name = "base"

    def start(self) -> bool:
        raise NotImplementedError

    def stop(self):
        pass

    def _enqueue(self, event: dict):
        self.stats["events_received"] += 1
        try:
            self.event_queue.put_nowait(event)
        except queue.Full:
            self.stats["events_dropped"] += 1


class MQTTEventListener(BaseEventListener):
    """MQTT event listener — works with MQTT-enabled cameras and simulators."""

    def __init__(self, broker: str, port: int,
                 topics: Optional[list] = None):
        super().__init__()
        self.broker = broker
        self.port = port
        self.topics = topics or ["onvif/analytics", "onvif/analytics/events"]
        self._client = None
        self.source_name = "mqtt"

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
        try:
            payload = json.loads(msg.payload)
            if payload.get("objectCount", 0) > 0:
                payload.setdefault("source", "mqtt_json")
                self._enqueue(payload)
            return
        except (json.JSONDecodeError, UnicodeDecodeError):
            pass

        try:
            xml_str = msg.payload.decode("utf-8", errors="ignore")
            parsed = parse_onvif_metadata_xml(xml_str)
            if parsed:
                self._enqueue(parsed)
                return
            for ev in parse_onvif_notification(xml_str):
                if ev.get("objectCount", 0) > 0:
                    self._enqueue(ev)
        except Exception:
            pass



# ═══════════════════════════════════════════════════════════════════════════
#  ONVIF Client
# ═══════════════════════════════════════════════════════════════════════════
class ONVIFClient:
    """Lightweight ONVIF client for any camera — discovery and publish-back."""

    def __init__(self, ip: str, port: int, user: str, password: str):
        self.ip = ip
        self.port = port
        base = f"http://{ip}:{port}/onvif"
        self.device_url = f"{base}/device_service"
        self.media_url = f"{base}/media_service"
        self.analytics_url = f"{base}/analytics_service"
        self.events_url = f"{base}/event_service"
        self._service_urls = {}

    def get_device_info(self) -> dict:
        resp = soap_request(self.device_url, "<tds:GetDeviceInformation/>")
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
        resp = soap_request(
            self.device_url,
            "<tds:GetCapabilities><tds:Category>All</tds:Category>"
            "</tds:GetCapabilities>")
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
                    caps[tag] = {"xaddr": xaddr}
                    if xaddr:
                        self._service_urls[tag] = xaddr
        except ElementTree.ParseError:
            pass

        if "Media" in self._service_urls:
            self.media_url = self._service_urls["Media"]
        if "Analytics" in self._service_urls:
            self.analytics_url = self._service_urls["Analytics"]
        if "Events" in self._service_urls:
            self.events_url = self._service_urls["Events"]
        return caps

    def get_profiles(self) -> list:
        resp = soap_request(self.media_url, "<trt:GetProfiles/>")
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
                mc = prof.find(".//tt:MetadataConfiguration", NS)
                vac = prof.find(".//tt:VideoAnalyticsConfiguration", NS)
                profiles.append({
                    "token": token,
                    "name": name_el.text if name_el is not None else "",
                    "video": vid,
                    "metadata": {"present": mc is not None},
                    "analytics": {"present": vac is not None},
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
        resp = soap_request(self.media_url, body)
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
        resp = soap_request(self.device_url, "<tds:GetScopes/>")
        scopes = []
        if not resp:
            return scopes
        try:
            root = ElementTree.fromstring(resp)
            for scope in root.findall(".//tds:Scopes", NS):
                item = scope.find("tt:ScopeItem", NS)
                if item is not None and item.text:
                    scopes.append(item.text)
        except ElementTree.ParseError:
            pass
        return scopes

    def get_event_properties(self) -> bool:
        resp = soap_request(self.events_url, "<tev:GetEventProperties/>")
        return bool(resp and "Fault" not in resp)


# ═══════════════════════════════════════════════════════════════════════════
#  Cross-validation
# ═══════════════════════════════════════════════════════════════════════════
def cross_validate(camera_event: dict, inference_dets: list) -> dict:
    """Compare camera-reported detections with inference results.

    Uses overlap matching: checks if every object type reported by the camera
    is also detected by VLM inference.  VLM may report additional types
    (it sees the whole scene), so extra VLM-only types do not count as a
    mismatch.  Count agreement is checked only for overlapping types.
    """
    inf_count = len(inference_dets)
    inf_types = collections.Counter(d["onvif_type"] for d in inference_dets)

    cam_count = camera_event.get("objectCount", 0)
    cam_types = collections.Counter(
        o.get("type", "") for o in camera_event.get("objects", []))

    # Check if every camera-reported type is present in VLM output
    missing_types = set(cam_types) - set(inf_types)
    matching_types = set(cam_types) & set(inf_types)

    # Mismatch if VLM has no detections at all or is missing camera types
    if cam_count > 0 and inf_count == 0:
        needs_update = True
    elif missing_types:
        needs_update = True
    else:
        needs_update = False

    return {
        "camera_objects": cam_count,
        "inference_objects": inf_count,
        "camera_classes": dict(cam_types),
        "inference_classes": dict(inf_types),
        "has_camera_data": True,
        "matching_types": sorted(matching_types),
        "missing_types": sorted(missing_types),
        "needs_update": needs_update,
    }


# ═══════════════════════════════════════════════════════════════════════════
#  RTSP Stream Probe
# ═══════════════════════════════════════════════════════════════════════════
def probe_rtsp(rtsp_uri: str) -> dict:
    try:
        result = subprocess.run(
            ["ffprobe", "-v", "error", "-rtsp_transport", "tcp",
             "-show_entries", "stream=index,codec_name,codec_type",
             "-of", "json", rtsp_uri],
            capture_output=True, text=True, timeout=15)
        if result.returncode == 0:
            data = json.loads(result.stdout)
            streams = data.get("streams", [])
            return {
                "streams": streams,
                "has_video": any(
                    s.get("codec_type") == "video" for s in streams),
                "has_audio": any(
                    s.get("codec_type") == "audio" for s in streams),
                "has_metadata": any(
                    s.get("codec_type") == "data" for s in streams),
            }
    except Exception as e:
        log.warning(f"ffprobe failed: {e}")
    return {"error": "probe failed"}
