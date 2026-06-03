#!/usr/bin/env python3
"""
ONVIF Camera Analytics Validation

Event-driven validation pipeline for ONVIF-enabled cameras. Connects to
an RTSP stream and MQTT event broker. Uses DLStreamer's ``gvagenai`` element
to run VLM (Visual Language Model) inference via OpenVINO GenAI inside the
GStreamer pipeline. When an MQTT analytics event arrives, pairs it with the
latest VLM inference result and displays the frame, MQTT event, and VLM
output on a live web dashboard with event history navigation.

Architecture:
  RTSP stream  ──► GStreamer pipeline ──► gvagenai (VLM, continuous) ──► VLM text
  MQTT events  ──► update prompt ──► wait for next VLM result
  Web dashboard ◄── frame + VLM text + MQTT event (with history)

Usage:
  python3 onvif_camera_analytics_validation.py --camera-ip 192.168.1.100
  python3 onvif_camera_analytics_validation.py --rtsp-uri rtsp://... --model-path ./Gemma3-4B
"""

# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import argparse
import http.server
import json
import logging
import os
import queue
import sys
import threading
import time
from urllib.parse import quote, urlparse, urlunparse

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstApp", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import GLib, Gst, GstAnalytics  # pylint: disable=no-name-in-module,wrong-import-position

# Add script directory to path so util is importable by pylint
if os.path.dirname(os.path.abspath(__file__)) not in sys.path:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from util import ONVIFClient, MQTTEventListener  # pylint: disable=wrong-import-position

log = logging.getLogger(__name__)

# ── VLM Prompts (edit these to change what the model looks for) ────────────

# Initial prompt used at pipeline start (before any MQTT event arrives).
# NOTE: This goes into the GStreamer pipeline string via Gst.parse_launch,
# so literal backslash-n (\\n) becomes a real newline in the C property.
INITIAL_PROMPT = (
    "You MUST respond using EXACTLY this format with these two headers "
    "on separate lines:\\n\\n"
    "**VLM Observation:** Describe only the objects you can clearly see "
    "in this image. State the type and count of each object.\\n\\n"
    "**Weapon Check:** State whether any weapon (gun, knife, rifle, etc.) "
    "is visible. If none, say 'No weapons detected'.\\n\\n"
    "Use the exact bold headers. Keep each section to 1-2 sentences."
)

# Dynamic prompt sent when an MQTT event arrives.
# The VLM is asked only for observation and weapon check.
# The MQTT Event section is pre-filled by the application.
DYNAMIC_PROMPT = (
    "The camera detected: {event_summary}. "
    "Verify by looking at the image.\n\n"
    "You MUST respond using EXACTLY this format with these two headers "
    "on separate lines:\n\n"
    "**VLM Observation:** <describe what you actually see in this image, "
    "confirm or deny the camera's detection above>\n\n"
    "**Weapon Check:** <state whether any weapon is visible. "
    "If none, say 'No weapons detected'>\n\n"
    "Use the exact bold headers. Keep each section to 1-2 sentences."
)

# Maximum characters from raw MQTT payload to include in VLM prompt.
_MAX_PAYLOAD_CHARS = 500


def _summarize_mqtt_payload(raw_payload: str) -> str:
    """Convert a raw MQTT payload into a plain-English summary.

    Tries to parse JSON and extract meaningful fields.  Falls back to
    the first _MAX_PAYLOAD_CHARS characters if parsing fails.
    """
    try:
        data = json.loads(raw_payload)
    except (json.JSONDecodeError, TypeError):
        # Not JSON — use raw text (truncated)
        text = raw_payload.strip()
        if len(text) > _MAX_PAYLOAD_CHARS:
            text = text[:_MAX_PAYLOAD_CHARS] + "...(truncated)"
        return text

    parts = []

    # Fields to skip — metadata/noise, not useful for VLM context
    _SKIP_KEYS = {"topic", "timestamp", "serial", "scenario", "resetTime"}

    # Extract nested message.data (common ONVIF/Axis format)
    msg = data.get("message", {})
    msg_data = msg.get("data", {}) if isinstance(msg, dict) else {}

    if msg_data:
        for key, val in msg_data.items():
            if key not in _SKIP_KEYS:
                parts.append(f"{key}={val}")
    else:
        # Fallback: list top-level keys
        for key, val in data.items():
            if key in _SKIP_KEYS:
                continue
            if isinstance(val, (str, int, float, bool)):
                parts.append(f"{key}={val}")

    return ", ".join(parts) if parts else raw_payload[:_MAX_PAYLOAD_CHARS]


# ═══════════════════════════════════════════════════════════════════════════
#  DLStreamer Pipeline (RTSP + gvagenai VLM)
# ═══════════════════════════════════════════════════════════════════════════


class DLStreamerVLMPipeline:  # pylint: disable=too-many-instance-attributes
    """GStreamer pipeline with DLStreamer gvagenai for VLM inference.

    Captures frames from an RTSP stream, runs VLM inference via gvagenai,
    and provides the latest JPEG frame and VLM text for the dashboard.
    Auto-reconnects on pipeline errors or EOS.
    """

    RECONNECT_DELAY = 2  # seconds between reconnection attempts

    def __init__(  # pylint: disable=too-many-arguments,too-many-positional-arguments
        self, rtsp_uri: str, model_path: str, device: str,
        max_tokens: int, frame_rate: float = 1.0,
        user: str = "", password: str = "",
    ):
        Gst.init(sys.argv)
        self._rtsp_uri = rtsp_uri
        self._model_path = model_path
        self._device = device
        self._max_tokens = max_tokens
        self._frame_rate = frame_rate
        self._user = user
        self._password = password
        self._pipeline = None
        self._loop = None
        self._thread = None
        self._running = False
        self._stopped = False  # True only when stop() is called
        self._lock = threading.Lock()
        self._latest_vlm_text = ""
        self._vlm_jpeg = None  # JPEG of the exact frame VLM processed
        self._vlm_count = 0
        self._vlm_ready = threading.Event()  # signalled when new VLM text
        self._current_prompt = None  # last set_property prompt, preserved across reconnects

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

    def _build_pipeline(self):
        """Create a GStreamer pipeline with gvagenai for VLM inference.

        gvagenai runs continuously at --frame-rate fps. The MQTT handler
        waits for the next VLM result rather than gating frames via a valve.
        """
        gen_cfg = f"max_new_tokens={self._max_tokens}"

        pipeline_str = (
            f'urisourcebin uri={self._rtsp_uri} name=src ! '
            f'decodebin3 ! '
            f'videoconvertscale ! '
            f'video/x-raw,format=RGB ! '
            f'queue max-size-buffers=1 leaky=downstream ! '
            f'videorate max-rate=1 ! '
            f'gvagenai '
            f'model-path="{self._model_path}" '
            f'device={self._device} '
            f'prompt="{INITIAL_PROMPT}" '
            f'generation-config="{gen_cfg}" '
            f'chunk-size=1 '
            f'frame-rate={self._frame_rate} '
            f'name=genai ! '
            f'jpegenc quality=50 ! '
            f'appsink name=vlm_jpeg_sink max-buffers=1 drop=true '
            f'emit-signals=true sync=false'
        )
        log.info("DLStreamer pipeline: %s", pipeline_str)
        pipeline = Gst.parse_launch(pipeline_str)

        src = pipeline.get_by_name("src")
        if src:
            src.connect("source-setup", self._source_setup)

        vlm_jpeg_sink = pipeline.get_by_name("vlm_jpeg_sink")
        if vlm_jpeg_sink:
            vlm_jpeg_sink.connect("new-sample", self._on_vlm_jpeg)

        # Add pad probe on gvagenai src pad to extract VLM results
        genai = pipeline.get_by_name("genai")
        if genai:
            src_pad = genai.get_static_pad("src")
            if src_pad:
                src_pad.add_probe(
                    Gst.PadProbeType.BUFFER, self._on_vlm_result)

        return pipeline

    def _on_vlm_result(self, _pad, info):
        """Pad probe on gvagenai src: extract VLM text from ClsMtd."""
        buf = info.get_buffer()
        if buf is None:
            return Gst.PadProbeReturn.OK

        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(buf)
        if not rmeta:
            return Gst.PadProbeReturn.OK

        idx = 0
        while True:
            found, cls_mtd = rmeta.get_cls_mtd(idx)
            if not found:
                break
            quark = cls_mtd.get_quark(0)
            if quark:
                label_text = GLib.quark_to_string(quark)
                if label_text:
                    with self._lock:
                        self._latest_vlm_text = label_text
                        self._vlm_count += 1
                        log.info("VLM [%d]: %s", self._vlm_count,
                                 label_text[:80])
                    # _vlm_ready is signalled in _on_vlm_jpeg after
                    # the JPEG of this frame is captured
                    break
            idx += 1

        return Gst.PadProbeReturn.OK

    def start(self) -> bool:
        """Build and start the GStreamer pipeline."""
        self._pipeline = self._build_pipeline()

        ret = self._pipeline.set_state(Gst.State.PLAYING)
        if ret == Gst.StateChangeReturn.FAILURE:
            bus = self._pipeline.get_bus()
            msg = bus.timed_pop_filtered(0, Gst.MessageType.ERROR)
            if msg:
                err, debug = msg.parse_error()
                log.error("DLStreamer pipeline failed to start: %s", err.message)
                log.error("  Debug: %s", debug)
            else:
                log.error("DLStreamer pipeline failed to start (no error details)")
            self._pipeline.set_state(Gst.State.NULL)
            return False

        self._running = True
        self._stopped = False
        self._thread = threading.Thread(target=self._run_loop, daemon=True)
        self._thread.start()
        log.info("DLStreamer pipeline started")
        return True

    def _run_loop(self):
        """Main thread: runs the GLib loop and auto-reconnects."""
        while not self._stopped:
            self._loop = GLib.MainLoop()
            bus = self._pipeline.get_bus()
            bus.add_signal_watch()
            bus.connect("message", self._on_bus_msg)
            self._running = True
            self._loop.run()

            if self._stopped:
                break

            log.warning("Pipeline stopped, reconnecting in %ds...",
                        self.RECONNECT_DELAY)
            self._pipeline.set_state(Gst.State.NULL)
            time.sleep(self.RECONNECT_DELAY)

            if self._stopped:
                break

            self._pipeline = self._build_pipeline()
            # Re-apply dynamic prompt that was set before reconnect
            if self._current_prompt:
                genai = self._pipeline.get_by_name("genai")
                if genai:
                    genai.set_property("prompt", self._current_prompt)
            ret = self._pipeline.set_state(Gst.State.PLAYING)
            if ret == Gst.StateChangeReturn.FAILURE:
                log.error("Reconnect failed, retrying...")
                continue
            log.info("Pipeline reconnected")

    def stop(self):
        """Stop the pipeline and release resources."""
        self._stopped = True
        self._running = False
        if self._loop:
            self._loop.quit()
        if self._pipeline:
            self._pipeline.set_state(Gst.State.NULL)
        if self._thread:
            self._thread.join(timeout=5)
        self._pipeline = None
        log.info("DLStreamer pipeline stopped")

    def get_vlm_jpeg(self) -> bytes:
        """Return the JPEG of the exact frame VLM inference processed."""
        with self._lock:
            return self._vlm_jpeg

    def get_vlm_text(self) -> str:
        """Return the latest VLM inference text."""
        with self._lock:
            return self._latest_vlm_text

    def request_inference(self, mqtt_event: dict = None,
                           timeout: float = 120.0) -> str:
        """Update the gvagenai prompt with MQTT context and wait for
        a new VLM result.

        The MQTT Event section is pre-filled by this method (not by the
        VLM).  The VLM only produces VLM Observation and Weapon Check.
        The combined three-section text is returned.
        """
        with self._lock:
            baseline_count = self._vlm_count

        # Update prompt with MQTT context if available
        skip = 1  # default: just wait for the next result
        mqtt_summary = ""
        mqtt_time = ""
        if mqtt_event:
            raw_event = mqtt_event.get("raw_payload", "")
            mqtt_summary = _summarize_mqtt_payload(raw_event)
            # Extract timestamp for display freshness
            ts_raw = mqtt_event.get("timestamp", "")
            if ts_raw:
                mqtt_time = time.strftime("%H:%M:%S")
            prompt = DYNAMIC_PROMPT.format(event_summary=mqtt_summary)
            genai = self._pipeline.get_by_name("genai") if self._pipeline else None
            if genai:
                genai.set_property("prompt", prompt)
                self._current_prompt = prompt
                skip = 2  # discard 1 in-flight (old-prompt) result
                log.info("Prompt updated (%d chars): %.200s",
                         len(prompt), prompt)

        target_count = baseline_count + skip
        self._vlm_ready.clear()

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self._vlm_ready.wait(timeout=max(0.1, deadline - time.monotonic())):
                with self._lock:
                    if self._vlm_count >= target_count:
                        vlm_text = self._latest_vlm_text
                        # Pre-fill MQTT Event section
                        if mqtt_summary:
                            tag = f"[{mqtt_time}] " if mqtt_time else ""
                            return (f"**MQTT Event:** {tag}{mqtt_summary}\n\n"
                                    + vlm_text)
                        return vlm_text
                # Result arrived but hasn't reached target; keep waiting
                self._vlm_ready.clear()

        log.warning("VLM inference timed out after %.0fs", timeout)
        vlm_text = self.get_vlm_text()
        if mqtt_summary:
            tag = f"[{mqtt_time}] " if mqtt_time else ""
            return f"**MQTT Event:** {tag}{mqtt_summary}\n\n" + vlm_text
        return vlm_text

    @property
    def vlm_count(self) -> int:
        with self._lock:
            return self._vlm_count

    def _on_vlm_jpeg(self, appsink):
        """Callback for VLM branch appsink: capture JPEG of VLM frame.

        This fires after gvagenai + jpegenc, guaranteeing the JPEG is
        the exact frame the VLM processed. Signals _vlm_ready.
        """
        sample = appsink.emit("pull-sample")
        if sample:
            buf = sample.get_buffer()
            ok, mi = buf.map(Gst.MapFlags.READ)
            if ok:
                with self._lock:
                    self._vlm_jpeg = bytes(mi.data)
                buf.unmap(mi)
        self._vlm_ready.set()
        return Gst.FlowReturn.OK

    def _on_bus_msg(self, _bus, msg):
        if msg.type == Gst.MessageType.ERROR:
            err, debug = msg.parse_error()
            log.error("GStreamer error: %s (%s)", err.message, debug)
            self._running = False
            self._loop.quit()
        elif msg.type == Gst.MessageType.EOS:
            log.info("GStreamer: end of stream, will reconnect")
            self._running = False
            self._loop.quit()


# ═══════════════════════════════════════════════════════════════════════════
#  Web Dashboard
# ═══════════════════════════════════════════════════════════════════════════

# Load dashboard HTML from file (once at import time)
_DASHBOARD_DIR = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(_DASHBOARD_DIR, "dashboard.html"), "r", encoding="utf-8") as _f:
    DASHBOARD_HTML = _f.read()


class _DashboardState:
    """Shared mutable state for the web dashboard."""
    event_history = []  # list of {frame_jpeg, mqtt_event, vlm_text}
    lock = threading.Lock()

    @classmethod
    def add_event(cls, frame_jpeg, mqtt_event, vlm_text):
        """Append a new event to the history."""
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
        """Return number of events in history."""
        with cls.lock:
            return len(cls.event_history)


class DashboardHandler(http.server.BaseHTTPRequestHandler):
    """HTTP handler for the live validation dashboard."""

    def do_GET(self):  # pylint: disable=invalid-name
        """Handle GET requests for dashboard pages, frames, and API endpoints."""
        if self.path in ('/', '/index.html'):
            self._respond(200, 'text/html', DASHBOARD_HTML.encode(),
                          extra={'Cache-Control': 'no-cache'})

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
                mqtt_ev = ev["mqtt_event"]
                raw_topic = mqtt_ev.get("topic", "")
                raw_payload = mqtt_ev.get("raw_payload", "")
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
        """Send an HTTP response with the given status, content-type, and body."""
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Access-Control-Allow-Origin', '*')
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        """Suppress default HTTP request logging."""


def start_web_ui(host: str, port: int):
    """Start the dashboard web server on the given host and port."""
    srv = http.server.ThreadingHTTPServer((host, port), DashboardHandler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    log.info("Web UI on %s:%d", host, port)
    return srv


# ═══════════════════════════════════════════════════════════════════════════
#  ONVIF Discovery
# ═══════════════════════════════════════════════════════════════════════════

def discover_camera(ip: str, port: int):
    """Query ONVIF device info, capabilities, profiles, stream URI, and event topics."""
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

    # Discover event topics
    event_topics = onvif.get_event_topics()
    if event_topics:
        print(f"  Event topics: {', '.join(event_topics[:5])}"
              f"{'...' if len(event_topics) > 5 else ''}")

    # Discover RTSP URI from first profile
    rtsp_uri = ""
    if profiles:
        rtsp_uri = onvif.get_stream_uri(profiles[0]["token"])
        if rtsp_uri:
            print(f"  RTSP URI (ONVIF): {rtsp_uri}")

    return rtsp_uri, event_topics


def build_rtsp_uri(args) -> str:
    """Determine the RTSP URI from args, ONVIF discovery, or fallback.

    Also updates args.mqtt_topics with discovered event topics if the
    user left the default wildcard and the camera provides topic info.
    """
    print("\n[Discovery] ONVIF Camera")
    print("-" * 60)

    rtsp_uri = args.rtsp_uri
    user_provided = bool(rtsp_uri)

    if not rtsp_uri:
        discovered_uri, discovered_topics = discover_camera(
            args.camera_ip, args.onvif_port)
        if discovered_uri:
            rtsp_uri = discovered_uri
        # Use discovered topics if user kept default wildcard
        if (discovered_topics
                and args.mqtt_topics == ["onvif/analytics/#"]):
            print(f"  Auto-discovered {len(discovered_topics)} event topic(s)")
            log.info("Using discovered MQTT topics: %s", discovered_topics)

    if not rtsp_uri:
        rtsp_uri = f"rtsp://{args.camera_ip}:554/stream1"
        print(f"  RTSP URI (fallback): {rtsp_uri}")

    # Embed credentials only for discovered/fallback URIs, not when
    # the user explicitly provided --rtsp-uri (the test server may
    # not support auth, and injecting credentials can cause crashes).
    if (not user_provided and args.onvif_user
            and '@' not in rtsp_uri):
        parsed = urlparse(rtsp_uri)
        userinfo = quote(args.onvif_user, safe='')
        if args.onvif_pass:
            userinfo += ':' + quote(args.onvif_pass, safe='')
        rtsp_uri = urlunparse(parsed._replace(
            netloc=f"{userinfo}@{parsed.hostname}"
                   + (f":{parsed.port}" if parsed.port else "")))
        print(f"  RTSP URI (with auth): rtsp://***@{parsed.hostname}"
              f"{':%d' % parsed.port if parsed.port else ''}{parsed.path}")  # pylint: disable=consider-using-f-string

    return rtsp_uri


# ═══════════════════════════════════════════════════════════════════════════
#  Validation Loop
# ═══════════════════════════════════════════════════════════════════════════

def validation_loop(pipeline: DLStreamerVLMPipeline,
                    listener: MQTTEventListener, web_port: int):
    """Event-driven loop: MQTT event → pair with latest VLM result → dashboard."""
    event_count = 0

    print("\n" + "=" * 60)
    print("  Listening for MQTT events ... (Ctrl+C to stop)")
    print("  VLM inference runs continuously via gvagenai.")
    print(f"  Web UI: http://localhost:{web_port}")
    print("=" * 60 + "\n")

    try:
        while True:
            try:
                event = listener.event_queue.get(timeout=1.0)
            except queue.Empty:
                continue

            # Drain stale events — keep only the latest
            skipped = 0
            while True:
                try:
                    newer = listener.event_queue.get_nowait()
                    skipped += 1
                    event = newer
                except queue.Empty:
                    break
            if skipped:
                log.info("Skipped %d stale MQTT events, using latest",
                         skipped)

            event_count += 1

            # Trigger VLM inference on the current frame
            vlm_text = pipeline.request_inference(mqtt_event=event)
            if not vlm_text:
                vlm_text = "(VLM inference timed out)"

            # Get the exact frame VLM processed
            processed_jpeg = pipeline.get_vlm_jpeg()
            if processed_jpeg is None:
                log.warning("No frame available, skipping event")
                continue

            # Store in history for dashboard
            _DashboardState.add_event(processed_jpeg, event, vlm_text)

            # Console output
            short = (vlm_text[:60] + "...") if len(vlm_text) > 60 else vlm_text
            print(f"  [{event_count:4d}] VLM: {short}")

    except KeyboardInterrupt:
        print("\n\n" + "=" * 60)
        print("  VALIDATION STOPPED")
        print("=" * 60)

    finally:
        print("\n  " + "─" * 40)
        print(f"  Events processed:   {event_count}")
        print(f"  VLM inferences:     {pipeline.vlm_count}")
        print(f"  MQTT events in:     {listener.stats['events_received']}")
        print(f"  MQTT events dropped:{listener.stats['events_dropped']}")
        print("=" * 60 + "\n")


# ═══════════════════════════════════════════════════════════════════════════
#  Main
# ═══════════════════════════════════════════════════════════════════════════

def run(args):
    """Run the ONVIF camera analytics validation pipeline."""
    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s [%(levelname)s] %(message)s")

    print("\n" + "=" * 60)
    print("  ONVIF Camera Analytics Validation (DLStreamer gvagenai)")
    print("=" * 60)

    # -- Resolve RTSP URI (via ONVIF or fallback) --
    rtsp_uri = build_rtsp_uri(args)

    # -- Validate VLM model path --
    print(f"\n[Setup] VLM model via gvagenai (device: {args.device})")
    print("-" * 60)
    if not args.model_path:
        print("  FATAL: --model-path is required (or set GENAI_MODEL_PATH)")
        print("  Export: optimum-cli export openvino --model google/gemma-3-4b-it"
              " --weight-format int4 --trust-remote-code Gemma3-4B")
        return
    print(f"  Model: {args.model_path}")
    print(f"  Device: {args.device}")
    print(f"  Max tokens: {args.max_tokens}")

    # -- Start DLStreamer pipeline with gvagenai --
    print("\n[Setup] DLStreamer pipeline (RTSP + gvagenai)")
    print("-" * 60)
    pipeline = DLStreamerVLMPipeline(
        rtsp_uri, args.model_path, args.device,
        args.max_tokens, args.frame_rate,
        user=args.onvif_user, password=args.onvif_pass)
    if not pipeline.start():
        print("  FATAL: DLStreamer pipeline failed to start")
        return
    print("  Pipeline: STARTED")

    # -- Connect MQTT --
    print(f"\n[Setup] MQTT ({args.mqtt_broker}:{args.mqtt_port})")
    print("-" * 60)
    listener = MQTTEventListener(
        args.mqtt_broker, args.mqtt_port, args.mqtt_topics)
    if not listener.start():
        print("  FATAL: MQTT connection failed")
        pipeline.stop()
        return
    for _ in range(20):
        if listener.connected:
            break
        time.sleep(0.1)
    if not listener.connected:
        print("  FATAL: MQTT connection timeout")
        listener.stop()
        pipeline.stop()
        return
    print(f"  MQTT: CONNECTED (topics: {', '.join(args.mqtt_topics)})")

    # -- Start web dashboard --
    start_web_ui(args.web_host, args.web_port)
    print(f"\n[Setup] Web UI: http://{args.web_host}:{args.web_port}")

    # -- Run validation loop --
    try:
        validation_loop(pipeline, listener, args.web_port)
    finally:
        pipeline.stop()
        listener.stop()


def main():
    """Parse arguments and run the validation pipeline."""
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
    p.add_argument("--frame-rate", type=float, default=0.2,
                   help="VLM inference rate in fps (default: 0.2 = every 5s)")
    p.add_argument("--max-tokens", type=int, default=256)
    p.add_argument("--mqtt-broker", default="localhost")
    p.add_argument("--mqtt-port", type=int, default=1883)
    p.add_argument("--mqtt-topics", nargs="+",
                   default=["onvif/analytics/#"])
    p.add_argument("--web-host", default="127.0.0.1",
                   help="Web dashboard bind address (use 0.0.0.0 only if needed)")
    p.add_argument("--web-port", type=int, default=8080)
    args = p.parse_args()
    run(args)


if __name__ == "__main__":
    main()
