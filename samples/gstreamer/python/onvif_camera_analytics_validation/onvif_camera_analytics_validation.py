#!/usr/bin/env python3
"""
ONVIF Camera Analytics Validation

Event-driven validation pipeline for ONVIF-enabled cameras. Connects to
an RTSP stream and MQTT event broker. When an MQTT analytics event arrives,
captures the current frame, runs VLM (Visual Language Model) inference via
OpenVINO GenAI, and displays the frame, MQTT event, and VLM output on a
live web dashboard with event history navigation.

Architecture:
  RTSP stream  ──► GStreamer (frame capture) ──► latest frame buffer
  MQTT events  ──► trigger ──► capture frame ──► VLM inference
  Web dashboard ◄── frame + VLM text + MQTT event (with history)

Usage:
  python3 onvif_camera_analytics_validation.py --camera-ip 192.168.1.100
  python3 onvif_camera_analytics_validation.py --rtsp-uri rtsp://... --model-path ./Gemma3-4B
"""

import argparse
import http.server
import json
import logging
import os
import queue
import threading
import time
from urllib.parse import quote, urlparse, urlunparse

import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstApp", "1.0")
from gi.repository import GLib, Gst

import numpy as np
import openvino as ov
import openvino_genai

from util import ONVIFClient, MQTTEventListener

log = logging.getLogger(__name__)


# ═══════════════════════════════════════════════════════════════════════════
#  RTSP Frame Capture (GStreamer)
# ═══════════════════════════════════════════════════════════════════════════


class RTSPCapture:
    """Captures frames from an RTSP stream via GStreamer.

    Maintains the latest RGB frame (for VLM) and JPEG frame (for web UI).
    """

    def __init__(self, rtsp_uri: str, frame_rate: int = 1,
                 user: str = "", password: str = ""):
        Gst.init(None)
        self._rtsp_uri = rtsp_uri
        self._frame_rate = frame_rate
        self._user = user
        self._password = password
        self._pipeline = None
        self._loop = None
        self._thread = None
        self._running = False
        self._lock = threading.Lock()
        self._latest_jpeg = None
        self._latest_rgb = None
        self._frame_w = 0
        self._frame_h = 0

    def _source_setup(self, _urisourcebin, source):
        """Configure rtspsrc for TCP transport and credentials."""
        factory = source.get_factory()
        if factory and factory.get_name() == "rtspsrc":
            source.set_property("protocols", "tcp")
            source.set_property("latency", 300)
            if self._user:
                source.set_property("user-id", self._user)
            if self._password:
                source.set_property("user-pw", self._password)
            log.info("rtspsrc configured: TCP, user=%s",
                     self._user or "(none)")

    def start(self) -> bool:
        pipeline_str = (
            f'urisourcebin uri={self._rtsp_uri} name=src ! '
            f'decodebin3 ! videoconvert n-threads=4 ! '
            f'video/x-raw,format=RGB ! '
            f'videorate max-rate={self._frame_rate} ! '
            f'tee name=t '
            f't. ! queue max-size-buffers=1 leaky=downstream ! '
            f'appsink name=rgb_sink max-buffers=1 drop=true '
            f'emit-signals=true sync=false '
            f't. ! queue max-size-buffers=1 leaky=downstream ! '
            f'jpegenc quality=50 ! '
            f'appsink name=jpeg_sink max-buffers=1 drop=true '
            f'emit-signals=true sync=false'
        )
        log.info("RTSP pipeline: %s", pipeline_str)
        self._pipeline = Gst.parse_launch(pipeline_str)

        src = self._pipeline.get_by_name("src")
        if src:
            src.connect("source-setup", self._source_setup)

        jpeg_sink = self._pipeline.get_by_name("jpeg_sink")
        if jpeg_sink:
            jpeg_sink.connect("new-sample", self._on_jpeg)

        rgb_sink = self._pipeline.get_by_name("rgb_sink")
        if rgb_sink:
            rgb_sink.connect("new-sample", self._on_rgb)

        ret = self._pipeline.set_state(Gst.State.PLAYING)
        if ret == Gst.StateChangeReturn.FAILURE:
            log.error("RTSP pipeline failed to start")
            return False

        self._running = True
        self._loop = GLib.MainLoop()
        self._thread = threading.Thread(target=self._bus_loop, daemon=True)
        self._thread.start()
        log.info("RTSP capture started")
        return True

    def stop(self):
        self._running = False
        if self._loop:
            self._loop.quit()
        if self._pipeline:
            self._pipeline.set_state(Gst.State.NULL)
        if self._thread:
            self._thread.join(timeout=5)
        self._pipeline = None
        log.info("RTSP capture stopped")

    def get_jpeg(self) -> bytes:
        with self._lock:
            return self._latest_jpeg

    def get_rgb_frame(self):
        """Return (rgb_bytes, width, height) or (None, 0, 0)."""
        with self._lock:
            return self._latest_rgb, self._frame_w, self._frame_h

    def _on_jpeg(self, appsink):
        sample = appsink.emit("pull-sample")
        if sample:
            buf = sample.get_buffer()
            ok, mi = buf.map(Gst.MapFlags.READ)
            if ok:
                with self._lock:
                    self._latest_jpeg = bytes(mi.data)
                buf.unmap(mi)
        return Gst.FlowReturn.OK

    def _on_rgb(self, appsink):
        sample = appsink.emit("pull-sample")
        if sample:
            caps = sample.get_caps()
            s = caps.get_structure(0)
            w, h = s.get_value("width"), s.get_value("height")
            buf = sample.get_buffer()
            ok, mi = buf.map(Gst.MapFlags.READ)
            if ok:
                with self._lock:
                    self._latest_rgb = bytes(mi.data)
                    self._frame_w = w
                    self._frame_h = h
                buf.unmap(mi)
        return Gst.FlowReturn.OK

    def _bus_loop(self):
        bus = self._pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect("message", self._on_bus_msg)
        self._loop.run()

    def _on_bus_msg(self, _bus, msg):
        if msg.type == Gst.MessageType.ERROR:
            err, debug = msg.parse_error()
            log.error("GStreamer error: %s (%s)", err.message, debug)
            self._running = False
            self._loop.quit()
        elif msg.type == Gst.MessageType.EOS:
            log.info("GStreamer: end of stream")


# ═══════════════════════════════════════════════════════════════════════════
#  VLM Inference Engine
# ═══════════════════════════════════════════════════════════════════════════

class VLMEngine:
    """Runs VLM inference on demand using OpenVINO GenAI VLMPipeline."""

    def __init__(self, model_path: str, device: str, prompt: str,
                 max_tokens: int):
        self._prompt = prompt
        self._count = 0
        self._lock = threading.Lock()
        self._latest_text = ""

        log.info("Loading VLMPipeline from %s on %s ...", model_path, device)
        self._pipe = openvino_genai.VLMPipeline(str(model_path), device)
        self._gen_config = openvino_genai.GenerationConfig(
            max_new_tokens=max_tokens)
        log.info("VLMPipeline loaded")

    def infer(self, rgb_data: bytes, width: int, height: int) -> str:
        """Run inference on an RGB frame. Returns the VLM text."""
        frame = np.frombuffer(rgb_data, dtype=np.uint8).reshape(
            height, width, 3)
        tensor = ov.Tensor(frame)
        self._pipe.start_chat()
        result = self._pipe.generate(
            self._prompt, image=tensor,
            generation_config=self._gen_config)
        self._pipe.finish_chat()
        text = str(result).strip()
        with self._lock:
            self._latest_text = text
            self._count += 1
        log.info("VLM [%d]: %s", self._count, text[:80])
        return text

    @property
    def latest_text(self) -> str:
        with self._lock:
            return self._latest_text

    @property
    def count(self) -> int:
        with self._lock:
            return self._count


# ═══════════════════════════════════════════════════════════════════════════
#  Web Dashboard
# ═══════════════════════════════════════════════════════════════════════════

# Load dashboard HTML from file (once at import time)
_DASHBOARD_DIR = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(_DASHBOARD_DIR, "dashboard.html"), "r") as _f:
    DASHBOARD_HTML = _f.read()


class _DashboardState:
    """Shared mutable state for the web dashboard."""
    event_history = []  # list of {frame_jpeg, mqtt_event, vlm_text}
    lock = threading.Lock()

    @classmethod
    def add_event(cls, frame_jpeg, mqtt_event, vlm_text):
        with cls.lock:
            cls.event_history.append({
                "frame_jpeg": frame_jpeg,
                "mqtt_event": mqtt_event,
                "vlm_text": vlm_text,
            })

    @classmethod
    def get_event(cls, idx):
        """Get event by 1-based index. Returns dict or None."""
        with cls.lock:
            if 1 <= idx <= len(cls.event_history):
                return cls.event_history[idx - 1]
        return None

    @classmethod
    def count(cls):
        with cls.lock:
            return len(cls.event_history)


class DashboardHandler(http.server.BaseHTTPRequestHandler):
    """HTTP handler for the live validation dashboard."""

    def do_GET(self):
        if self.path in ('/', '/index.html'):
            self._respond(200, 'text/html', DASHBOARD_HTML.encode())

        elif self.path.startswith('/frame/'):
            try:
                idx = int(self.path.split('/')[2].split('?')[0])
            except (IndexError, ValueError):
                self.send_error(404)
                return
            ev = _DashboardState.get_event(idx)
            if ev and ev["frame_jpeg"]:
                self._respond(200, 'image/jpeg', ev["frame_jpeg"],
                              extra={'Cache-Control': 'no-cache'})
            else:
                self.send_error(404, "No frame")

        elif self.path == '/api/status':
            data = {"event_count": _DashboardState.count()}
            self._respond(200, 'application/json',
                          json.dumps(data).encode())

        elif self.path.startswith('/api/event/'):
            try:
                idx = int(self.path.split('/')[3])
            except (IndexError, ValueError):
                self.send_error(404)
                return
            ev = _DashboardState.get_event(idx)
            if ev:
                mqtt = ev["mqtt_event"]
                raw_topic = mqtt.get("raw_topic", "")
                raw_payload = mqtt.get("raw_mqtt", "")
                # Pretty-print JSON payloads
                try:
                    raw_payload = json.dumps(
                        json.loads(raw_payload), indent=2)
                except (json.JSONDecodeError, TypeError):
                    pass
                data = {
                    "vlm_text": ev["vlm_text"],
                    "mqtt_topic": raw_topic,
                    "mqtt_payload": raw_payload,
                }
                self._respond(200, 'application/json',
                              json.dumps(data, default=str).encode())
            else:
                self.send_error(404)

        else:
            self.send_error(404)

    def _respond(self, code, ctype, body, extra=None):
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Access-Control-Allow-Origin', '*')
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


def start_web_ui(port: int):
    srv = http.server.ThreadingHTTPServer(('0.0.0.0', port), DashboardHandler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    log.info("Web UI on port %d", port)
    return srv


# ═══════════════════════════════════════════════════════════════════════════
#  ONVIF Discovery
# ═══════════════════════════════════════════════════════════════════════════

def discover_camera(ip: str, port: int):
    """Query ONVIF device info, capabilities, profiles, and stream URI."""
    onvif = ONVIFClient(ip, port)

    dev_info = onvif.get_device_info()
    if dev_info:
        print(f"  Device: {dev_info.get('Manufacturer', 'N/A')} "
              f"{dev_info.get('Model', 'N/A')} "
              f"FW:{dev_info.get('FirmwareVersion', 'N/A')}")
    else:
        print("  WARNING: Could not retrieve device info")

    caps = onvif.get_capabilities()
    print(f"  Capabilities: Media={'Media' in caps}, "
          f"Analytics={'Analytics' in caps}, Events={'Events' in caps}")

    scopes = onvif.get_scopes()
    profile_m = any("Profile/M" in s for s in scopes)
    print(f"  Profile M: {'FOUND' if profile_m else 'NOT FOUND'}")

    profiles = onvif.get_profiles()
    for p in profiles:
        v = p.get("video", {})
        print(f"  Profile: {p['token']} | {v.get('encoding', '')} "
              f"{v.get('width', '')}x{v.get('height', '')}")

    # Discover RTSP URI from first profile
    rtsp_uri = ""
    if profiles:
        rtsp_uri = onvif.get_stream_uri(profiles[0]["token"])
        if rtsp_uri:
            print(f"  RTSP URI (ONVIF): {rtsp_uri}")

    return rtsp_uri


def build_rtsp_uri(args) -> str:
    """Determine the RTSP URI from args, ONVIF discovery, or fallback."""
    print("\n[Discovery] ONVIF Camera")
    print("-" * 60)

    rtsp_uri = args.rtsp_uri

    if not rtsp_uri:
        discovered = discover_camera(args.camera_ip, args.onvif_port)
        if discovered:
            rtsp_uri = discovered

    if not rtsp_uri:
        rtsp_uri = f"rtsp://{args.camera_ip}:554/stream1"
        print(f"  RTSP URI (fallback): {rtsp_uri}")

    # Embed credentials if provided and not already in URI
    if args.onvif_user and '@' not in rtsp_uri:
        parsed = urlparse(rtsp_uri)
        userinfo = quote(args.onvif_user, safe='')
        if args.onvif_pass:
            userinfo += ':' + quote(args.onvif_pass, safe='')
        rtsp_uri = urlunparse(parsed._replace(
            netloc=f"{userinfo}@{parsed.hostname}"
                   + (f":{parsed.port}" if parsed.port else "")))
        print(f"  RTSP URI (with auth): rtsp://***@{parsed.hostname}"
              f"{':%d' % parsed.port if parsed.port else ''}{parsed.path}")

    return rtsp_uri


# ═══════════════════════════════════════════════════════════════════════════
#  Validation Loop
# ═══════════════════════════════════════════════════════════════════════════

def validation_loop(capture: RTSPCapture, vlm: VLMEngine,
                    listener: MQTTEventListener, web_port: int):
    """Event-driven loop: MQTT event → capture frame → VLM → dashboard."""
    event_count = 0

    print(f"\n{'=' * 60}")
    print(f"  Listening for MQTT events ... (Ctrl+C to stop)")
    print(f"  Web UI: http://localhost:{web_port}")
    print(f"{'=' * 60}\n")

    try:
        while True:
            try:
                event = listener.event_queue.get(timeout=1.0)
            except queue.Empty:
                continue

            event_count += 1

            # Capture current frame
            rgb_data, w, h = capture.get_rgb_frame()
            if rgb_data is None:
                log.warning("No frame available, skipping event")
                continue

            processed_jpeg = capture.get_jpeg()

            # Run VLM inference
            try:
                vlm_text = vlm.infer(rgb_data, w, h)
            except Exception as e:
                log.error("VLM inference error: %s", e)
                continue

            # Store in history for dashboard
            _DashboardState.add_event(processed_jpeg, event, vlm_text)

            # Console output
            short = (vlm_text[:60] + "...") if len(vlm_text) > 60 else vlm_text
            print(f"  [{event_count:4d}] VLM: {short}")

    except KeyboardInterrupt:
        print(f"\n\n{'=' * 60}")
        print(f"  VALIDATION STOPPED")
        print(f"{'=' * 60}")

    finally:
        print(f"\n  {'─' * 40}")
        print(f"  Events processed:   {event_count}")
        print(f"  VLM inferences:     {vlm.count}")
        print(f"  MQTT events in:     {listener.stats['events_received']}")
        print(f"  MQTT events dropped:{listener.stats['events_dropped']}")
        print(f"{'=' * 60}\n")


# ═══════════════════════════════════════════════════════════════════════════
#  Main
# ═══════════════════════════════════════════════════════════════════════════

def run(args):
    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s [%(levelname)s] %(message)s")

    print("\n" + "=" * 60)
    print("  ONVIF Camera Analytics Validation (VLM)")
    print("=" * 60)

    # -- Resolve RTSP URI (via ONVIF or fallback) --
    rtsp_uri = build_rtsp_uri(args)

    # -- Load VLM model --
    print(f"\n[Setup] VLM model (device: {args.device})")
    print("-" * 60)
    if not args.model_path:
        print("  FATAL: --model-path is required (or set GENAI_MODEL_PATH)")
        print("  Export: optimum-cli export openvino --model google/gemma-3-4b-it"
              " --weight-format int4 --trust-remote-code Gemma3-4B")
        return
    print(f"  Model: {args.model_path}")
    print(f"  Device: {args.device}")
    print(f"  Max tokens: {args.max_tokens}")

    vlm = VLMEngine(args.model_path, args.device, args.prompt, args.max_tokens)

    # -- Start RTSP capture --
    print(f"\n[Setup] RTSP capture")
    print("-" * 60)
    capture = RTSPCapture(rtsp_uri, args.frame_rate,
                          user=args.onvif_user, password=args.onvif_pass)
    if not capture.start():
        print("  FATAL: RTSP capture failed to start")
        return
    print("  RTSP: STARTED")

    # -- Connect MQTT --
    print(f"\n[Setup] MQTT ({args.mqtt_broker}:{args.mqtt_port})")
    print("-" * 60)
    listener = MQTTEventListener(
        args.mqtt_broker, args.mqtt_port, args.mqtt_topics)
    if not listener.start():
        print("  FATAL: MQTT connection failed")
        capture.stop()
        return
    for _ in range(20):
        if listener.connected:
            break
        time.sleep(0.1)
    if not listener.connected:
        print("  FATAL: MQTT connection timeout")
        listener.stop()
        capture.stop()
        return
    print(f"  MQTT: CONNECTED (topics: {', '.join(args.mqtt_topics)})")

    # -- Start web dashboard --
    start_web_ui(args.web_port)
    print(f"\n[Setup] Web UI: http://localhost:{args.web_port}")

    # -- Run validation loop --
    try:
        validation_loop(capture, vlm, listener, args.web_port)
    finally:
        capture.stop()
        listener.stop()


def main():
    p = argparse.ArgumentParser(
        description="ONVIF Camera Analytics Validation — "
                    "event-driven VLM cross-validation",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Examples:
  %(prog)s --camera-ip 192.168.1.100 --model-path ./Gemma3-4B
  %(prog)s --camera-ip 192.168.1.100 --device GPU --model-path ./Gemma3-4B
  %(prog)s --rtsp-uri rtsp://cam:554/stream1 --model-path ./Gemma3-4B

Model export (run once):
  pip install optimum-intel openvino
  optimum-cli export openvino --model google/gemma-3-4b-it \\
      --weight-format int4 --trust-remote-code Gemma3-4B
""")
    p.add_argument("--camera-ip", default="192.168.1.100")
    p.add_argument("--onvif-port", type=int, default=80)
    p.add_argument("--onvif-user", default="admin")
    p.add_argument("--onvif-pass", default="admin")
    p.add_argument("--rtsp-uri", default="",
                   help="RTSP URI (auto-discovered via ONVIF if omitted)")
    p.add_argument("--model-path",
                   default=os.environ.get("GENAI_MODEL_PATH", ""),
                   help="Path to OpenVINO VLM model directory")
    p.add_argument("--device", default="CPU",
                   choices=["CPU", "GPU", "NPU", "AUTO"])
    p.add_argument("--prompt",
                   default="Describe only the objects you can clearly see in "
                           "this image. State the count of each object type. "
                           "Do not guess or assume objects that are not visible.")
    p.add_argument("--frame-rate", type=int, default=1,
                   help="Frame sampling rate in fps (default: 1)")
    p.add_argument("--max-tokens", type=int, default=150)
    p.add_argument("--mqtt-broker", default="localhost")
    p.add_argument("--mqtt-port", type=int, default=1883)
    p.add_argument("--mqtt-topics", nargs="+",
                   default=["onvif/analytics/#"])
    p.add_argument("--web-port", type=int, default=8080)
    args = p.parse_args()
    run(args)


if __name__ == "__main__":
    main()
