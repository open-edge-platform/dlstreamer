#!/usr/bin/env python3
"""
ONVIF Profile M Analytics Validation Pipeline (VLM Edition)

Works with any ONVIF-enabled camera. Receives analytics events via MQTT,
runs VLM (Visual Language Model) inference using Intel DLStreamer (gvagenai),
cross-validates against the camera's events, and shows results in a web UI.

Architecture:
  1. Discover camera via ONVIF SOAP (device info, profiles, stream URI)
  2. DLStreamer GStreamer pipeline: RTSP → gvagenai VLM inference
  3. Receive analytics events from camera via MQTT
  4. Cross-validate VLM descriptions vs camera-reported events
  5. Visualise results in a live web dashboard

Usage:
  python3 onvif_camera_analytics_validation.py --camera-ip 192.168.1.100
  python3 onvif_camera_analytics_validation.py --camera-ip 192.168.1.100 --device GPU
  python3 onvif_camera_analytics_validation.py --rtsp-uri rtsp://... --model-path ./MiniCPM-V-2_6
"""

import argparse
import http.server
import json
import logging
import os
import queue
import re
import threading
import time

import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstApp", "1.0")
from gi.repository import GLib, Gst, GstApp

from util import (
    ONVIFClient,
    MQTTEventListener,
    cross_validate,
    probe_rtsp,
)

# ═══════════════════════════════════════════════════════════════════════════
#  VLM Configuration
# ═══════════════════════════════════════════════════════════════════════════

# Keyword map: extract object types from VLM text descriptions
OBJECT_KEYWORDS = {
    "Human": ["person", "people", "man", "woman", "child", "children",
              "pedestrian", "human", "boy", "girl", "worker", "crowd"],
    "Vehicle": ["car", "truck", "bus", "van", "vehicle", "motorcycle",
                "bicycle", "bike", "automobile", "taxi", "suv"],
    "Animal": ["dog", "cat", "bird", "animal", "horse", "sheep", "cow",
               "deer", "rabbit", "squirrel"],
}

VLM_OUTPUT_FILE = "/tmp/onvif_vlm_output.json"


def extract_objects_from_text(text: str) -> list:
    """Parse VLM text description into pseudo-detections for cross_validate().

    Scans the text for object keywords and returns a detection-like list
    that cross_validate() can compare with camera events.
    """
    if not text:
        return []
    text_lower = text.lower()
    detections = []
    seen = set()
    for onvif_type, keywords in OBJECT_KEYWORDS.items():
        for kw in keywords:
            matches = re.findall(
                r'\b' + re.escape(kw) + r'(?:s|es|ren)?\b', text_lower)
            for m in matches:
                key = (onvif_type, kw)
                if key not in seen:
                    seen.add(key)
                    detections.append({
                        "onvif_type": onvif_type,
                        "label": kw,
                        "confidence": 0.0,
                        "bbox": {},
                    })
    return detections


def _on_source_setup(_urisourcebin, source):
    """Configure rtspsrc for TCP transport when urisourcebin creates it."""
    factory = source.get_factory()
    if factory and factory.get_name() == "rtspsrc":
        source.set_property("protocols", "tcp")
        source.set_property("latency", 300)


# ═══════════════════════════════════════════════════════════════════════════
#  DLStreamer VLM Pipeline (lazy-start + idle-timeout)
# ═══════════════════════════════════════════════════════════════════════════

DEFAULT_IDLE_TIMEOUT = 60  # VLM is slower, use longer timeout

class DLStreamerPipeline:
    """Runs a GStreamer/DLStreamer gvagenai VLM pipeline on-demand.

    Lazy-start: pipeline is not created until ensure_running() is called.
    Idle-timeout: pipeline auto-stops after idle_timeout seconds without
    a call to ensure_running().

    Produces:
      - JPEG frames (for web UI) via jpegenc → appsink
      - VLM text results via gvametapublish → JSON file
    """

    def __init__(self, rtsp_uri, model_path, device, prompt,
                 frame_rate, chunk_size, max_tokens,
                 idle_timeout=DEFAULT_IDLE_TIMEOUT):
        Gst.init(None)
        self._rtsp_uri = rtsp_uri
        self._model_path = model_path
        self._device = device
        self._prompt = prompt
        self._frame_rate = frame_rate
        self._chunk_size = chunk_size
        self._max_tokens = max_tokens
        self._idle_timeout = idle_timeout
        self._pipeline = None
        self._lock = threading.Lock()
        self._running = False
        self._loop = None
        self._thread = None
        self._vlm_reader_thread = None
        self._idle_timer = None
        self._last_activity = 0.0
        self._latest_frame_jpeg = None
        self._latest_vlm_text = ""
        self._vlm_count = 0

    def ensure_running(self) -> bool:
        """Start the pipeline if not already running. Reset idle timer."""
        self._last_activity = time.monotonic()
        self._reset_idle_timer()

        if self._running and self._pipeline:
            return True

        return self._start()

    def _start(self) -> bool:
        """Build and start the GStreamer VLM pipeline."""
        if self._pipeline:
            self._teardown()

        # Clear VLM output file
        try:
            with open(VLM_OUTPUT_FILE, 'w') as f:
                pass
        except OSError:
            pass

        # Pipeline: urisourcebin → decode → videorate (throttle) → gvagenai → tee
        #   branch 1: gvametapublish (VLM text → JSON file)
        #   branch 2: jpegenc → appsink (frames for web UI)
        #
        # videorate before gvagenai drops frames at the GStreamer level so
        # only --frame-rate fps reach the VLM, avoiding wasted inference.
        pipeline_str = (
            f'urisourcebin uri={self._rtsp_uri} name=src ! '
            f'decodebin3 ! videoconvert n-threads=4 ! '
            f'video/x-raw,format=RGB ! '
            f'videorate max-rate={self._frame_rate} ! '
            f'gvagenai name=genai ! '
            f'tee name=t '
            f't. ! queue max-size-buffers=2 leaky=downstream ! '
            f'gvametapublish file-path={VLM_OUTPUT_FILE} ! '
            f'fakesink async=false '
            f't. ! queue max-size-buffers=1 leaky=downstream ! '
            f'jpegenc quality=50 ! '
            f'appsink name=frame_sink max-buffers=1 drop=true '
            f'emit-signals=true sync=false'
        )

        logging.info(f"DLStreamer VLM pipeline: {pipeline_str}")
        self._pipeline = Gst.parse_launch(pipeline_str)

        # Set gvagenai properties programmatically (handles special chars)
        genai = self._pipeline.get_by_name("genai")
        if genai:
            genai.set_property("model-path", self._model_path)
            genai.set_property("device", self._device)
            genai.set_property("prompt", self._prompt)
            genai.set_property("frame-rate", self._frame_rate)
            genai.set_property("chunk-size", self._chunk_size)
            genai.set_property("generation-config",
                               f"max_new_tokens={self._max_tokens}")

        # TCP transport for RTSP
        src = self._pipeline.get_by_name("src")
        if src:
            src.connect("source-setup", _on_source_setup)

        # JPEG frame capture callback
        frame_sink = self._pipeline.get_by_name("frame_sink")
        if frame_sink:
            frame_sink.connect("new-sample", self._on_new_frame)

        ret = self._pipeline.set_state(Gst.State.PLAYING)
        if ret == Gst.StateChangeReturn.FAILURE:
            logging.error("DLStreamer: VLM pipeline failed to start")
            self._teardown()
            return False

        self._running = True
        self._loop = GLib.MainLoop()
        self._thread = threading.Thread(target=self._bus_loop, daemon=True)
        self._thread.start()

        # Background thread to read VLM results from gvametapublish output
        self._vlm_reader_thread = threading.Thread(
            target=self._read_vlm_output, daemon=True)
        self._vlm_reader_thread.start()

        logging.info("DLStreamer: VLM pipeline started")
        return True

    def _teardown(self):
        """Stop and discard the current pipeline."""
        self._running = False
        if self._loop:
            self._loop.quit()
        if self._pipeline:
            self._pipeline.set_state(Gst.State.NULL)
        if self._thread:
            self._thread.join(timeout=5)
        if self._vlm_reader_thread:
            self._vlm_reader_thread.join(timeout=2)
        self._pipeline = None
        self._loop = None
        self._thread = None
        self._vlm_reader_thread = None
        logging.info("DLStreamer: VLM pipeline stopped")

    def _reset_idle_timer(self):
        if self._idle_timer:
            self._idle_timer.cancel()
        self._idle_timer = threading.Timer(
            self._idle_timeout, self._on_idle_timeout)
        self._idle_timer.daemon = True
        self._idle_timer.start()

    def _on_idle_timeout(self):
        if self._running:
            elapsed = time.monotonic() - self._last_activity
            if elapsed >= self._idle_timeout - 1:
                logging.info(
                    f"DLStreamer: idle for {elapsed:.0f}s, stopping pipeline")
                self._teardown()

    def stop(self):
        """Permanent shutdown (called at program exit)."""
        if self._idle_timer:
            self._idle_timer.cancel()
        self._teardown()

    def get_vlm_result(self) -> str:
        """Return the latest VLM text description."""
        with self._lock:
            return self._latest_vlm_text

    def get_frame_jpeg(self) -> bytes:
        """Return the latest JPEG frame bytes (for web UI)."""
        with self._lock:
            return self._latest_frame_jpeg

    def get_vlm_count(self) -> int:
        with self._lock:
            return self._vlm_count

    def _on_new_frame(self, appsink):
        """Store latest JPEG frame from the jpegenc branch."""
        sample = appsink.emit("pull-sample")
        if sample:
            buf = sample.get_buffer()
            ok, mapinfo = buf.map(Gst.MapFlags.READ)
            if ok:
                with self._lock:
                    self._latest_frame_jpeg = bytes(mapinfo.data)
                buf.unmap(mapinfo)
        return Gst.FlowReturn.OK

    def _read_vlm_output(self):
        """Background thread: tail the gvametapublish JSON file for VLM text."""
        last_size = 0
        while self._running:
            try:
                size = os.path.getsize(VLM_OUTPUT_FILE)
                if size > last_size:
                    with open(VLM_OUTPUT_FILE, 'r') as f:
                        f.seek(max(0, last_size))
                        new_data = f.read()
                    last_size = size

                    for line in reversed(new_data.strip().split('\n')):
                        line = line.strip().strip('[],')
                        if not line:
                            continue
                        try:
                            result = json.loads(line)
                            text = self._extract_text(result)
                            if text:
                                with self._lock:
                                    self._latest_vlm_text = text
                                    self._vlm_count += 1
                                break
                        except json.JSONDecodeError:
                            continue
            except FileNotFoundError:
                pass
            except OSError:
                pass
            time.sleep(1.0)

    @staticmethod
    def _extract_text(result: dict) -> str:
        """Extract VLM text from gvametapublish JSON output."""
        if result.get("result"):
            return result["result"]
        for obj in result.get("objects", []):
            if obj.get("label"):
                return obj["label"]
            for tensor in obj.get("tensors", []):
                if tensor.get("label"):
                    return tensor["label"]
        return result.get("text", "")

    def _bus_loop(self):
        bus = self._pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect("message", self._on_bus_message)
        self._loop.run()

    def _on_bus_message(self, bus, msg):
        if msg.type == Gst.MessageType.ERROR:
            err, debug = msg.parse_error()
            logging.error(f"DLStreamer error: {err.message} ({debug})")
            self._running = False
            self._loop.quit()
        elif msg.type == Gst.MessageType.EOS:
            logging.info("DLStreamer: end of stream")
            self._running = False
            self._loop.quit()


# ═══════════════════════════════════════════════════════════════════════════
#  Web UI Dashboard
# ═══════════════════════════════════════════════════════════════════════════

DASHBOARD_HTML = """\
<!DOCTYPE html>
<html><head>
<title>ONVIF VLM Validation Dashboard</title>
<meta charset="utf-8">
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body { font-family: system-ui, -apple-system, sans-serif;
         background:#1a1a2e; color:#e0e0e0; padding:20px; }
  h1 { color:#00d4ff; margin-bottom:20px; font-size:22px; }
  .grid { display:grid; grid-template-columns:1fr 1fr; gap:16px;
           margin-bottom:16px; }
  .panel { background:#16213e; border-radius:8px; padding:16px;
           border:1px solid #0f3460; margin-bottom:16px; }
  .panel h2 { color:#00d4ff; font-size:13px; margin-bottom:10px;
              text-transform:uppercase; letter-spacing:1px; }
  #frame { max-width:100%; border-radius:4px; background:#000;
           min-height:240px; display:block; }
  .vlm-text { font-size:15px; line-height:1.6; white-space:pre-wrap;
              color:#a8e6cf; min-height:60px; }
  .event-data { font-size:13px; color:#ffd3b6; font-family:monospace;
                white-space:pre-wrap; max-height:200px; overflow-y:auto; }
  .stats-grid { display:grid; grid-template-columns:repeat(3, 1fr);
                gap:12px; }
  .stat-box { background:#0f3460; border-radius:6px; padding:12px;
              text-align:center; }
  .stat-val { font-size:28px; font-weight:bold; color:#00d4ff; }
  .stat-lbl { font-size:11px; color:#888; text-transform:uppercase;
              margin-top:4px; }
  .tag { display:inline-block; padding:2px 8px; border-radius:4px;
         font-size:12px; margin:2px; font-weight:bold; }
  .match { background:#2d6a4f; color:#a8e6cf; }
  .mismatch { background:#9b2226; color:#ffc9c9; }
  .waiting { color:#666; font-style:italic; }
  @media (max-width:800px) {
    .grid { grid-template-columns:1fr; }
    .stats-grid { grid-template-columns:repeat(2, 1fr); }
  }
</style>
</head><body>
<h1>ONVIF Profile M &mdash; VLM Analytics Validation</h1>

<div class="grid">
  <div class="panel">
    <h2>Camera Frame</h2>
    <img id="frame" src="/frame.jpg" alt="waiting for frame..."/>
  </div>
  <div class="panel">
    <h2>VLM Description</h2>
    <div id="vlm" class="vlm-text waiting">Waiting for VLM inference...</div>
    <h2 style="margin-top:16px">Cross-Validation</h2>
    <div id="validation" class="event-data">Waiting...</div>
  </div>
</div>

<div class="panel">
  <h2>Latest Camera Event (MQTT)</h2>
  <pre id="event" class="event-data">Waiting for events...</pre>
</div>

<div class="panel">
  <h2>Stats</h2>
  <div id="stats" class="stats-grid"></div>
</div>

<script>
function fmt(obj) {
  if (!obj || Object.keys(obj).length === 0) return '(none)';
  return JSON.stringify(obj, null, 2);
}
function renderStats(s) {
  if (!s || Object.keys(s).length === 0) return '';
  var items = [
    ['Events', s.events_processed || 0],
    ['Validated', s.events_validated || 0],
    ['VLM Inferences', s.vlm_inferences || 0],
    ['Camera Objects', s.total_camera_objects || 0],
    ['VLM Objects', s.total_vlm_objects || 0],
    ['Mismatches', s.mismatches || 0]
  ];
  return items.map(function(a) {
    return '<div class="stat-box"><div class="stat-val">'+a[1]+'</div>'+
           '<div class="stat-lbl">'+a[0]+'</div></div>';
  }).join('');
}
function renderValidation(v) {
  if (!v || !v.camera_classes) return '<span class="waiting">Waiting...</span>';
  var status = v.needs_update
    ? '<span class="tag mismatch">MISMATCH</span>'
    : '<span class="tag match">MATCH</span>';
  return status +
    '\\nCamera: ' + v.camera_objects + ' objects ' + JSON.stringify(v.camera_classes) +
    '\\nVLM:    ' + v.inference_objects + ' objects ' + JSON.stringify(v.inference_classes);
}
function update() {
  fetch('/api/status').then(function(r) { return r.json(); }).then(function(d) {
    var vlmEl = document.getElementById('vlm');
    if (d.vlm_text) {
      vlmEl.textContent = d.vlm_text;
      vlmEl.className = 'vlm-text';
    }
    document.getElementById('event').textContent = fmt(d.latest_event);
    document.getElementById('validation').innerHTML = renderValidation(d.validation);
    document.getElementById('stats').innerHTML = renderStats(d.stats);
    var img = document.getElementById('frame');
    var newSrc = '/frame.jpg?' + Date.now();
    var tmp = new Image();
    tmp.onload = function() { img.src = newSrc; };
    tmp.src = newSrc;
  }).catch(function() {});
}
setInterval(update, 2000);
update();
</script>
</body></html>
"""


class DashboardHandler(http.server.BaseHTTPRequestHandler):
    """Minimal HTTP handler for the live validation dashboard."""

    pipeline = None
    latest_event = {}
    latest_validation = {}
    latest_vlm_text = ""
    stats = {}

    def do_GET(self):
        if self.path == '/' or self.path == '/index.html':
            self._respond(200, 'text/html', DASHBOARD_HTML.encode())

        elif self.path.startswith('/frame.jpg'):
            frame = None
            if self.__class__.pipeline:
                frame = self.__class__.pipeline.get_frame_jpeg()
            if frame:
                self._respond(200, 'image/jpeg', frame,
                              extra={'Cache-Control': 'no-cache'})
            else:
                self.send_error(404, "No frame available yet")

        elif self.path == '/api/status':
            data = {
                "vlm_text": self.__class__.latest_vlm_text,
                "latest_event": self.__class__.latest_event,
                "validation": self.__class__.latest_validation,
                "stats": self.__class__.stats,
            }
            body = json.dumps(data, default=str).encode()
            self._respond(200, 'application/json', body)

        else:
            self.send_error(404)

    def _respond(self, code, content_type, body, extra=None):
        self.send_response(code)
        self.send_header('Content-Type', content_type)
        self.send_header('Access-Control-Allow-Origin', '*')
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass  # silence access logs


def start_web_ui(port, pipeline):
    """Start the dashboard web server in a background thread."""
    DashboardHandler.pipeline = pipeline
    server = http.server.ThreadingHTTPServer(('0.0.0.0', port), DashboardHandler)
    t = threading.Thread(target=server.serve_forever, daemon=True)
    t.start()
    logging.info(f"Web UI started on port {port}")
    return server


# ═══════════════════════════════════════════════════════════════════════════
#  Main
# ═══════════════════════════════════════════════════════════════════════════

def run(args):
    logging.basicConfig(level=logging.INFO,
                        format="%(asctime)s [%(levelname)s] %(message)s")

    print("\n" + "=" * 70)
    print("  ONVIF Profile M — VLM Analytics Validation Pipeline")
    print("=" * 70)

    # ── ONVIF Discovery ──────────────────────────────────────────────
    print("\n[Discovery] ONVIF Camera")
    print("-" * 70)

    onvif = ONVIFClient(args.camera_ip, args.onvif_port,
                        args.onvif_user, args.onvif_pass)
    dev_info = onvif.get_device_info()
    if dev_info:
        print(f"  Device: {dev_info.get('Manufacturer', 'N/A')} "
              f"{dev_info.get('Model', 'N/A')} "
              f"FW:{dev_info.get('FirmwareVersion', 'N/A')}")
    else:
        print("  WARNING: Could not retrieve device info "
              "(camera may not be ONVIF-compliant)")

    caps = onvif.get_capabilities()
    has_events = "Events" in caps
    has_analytics = "Analytics" in caps
    print(f"  Capabilities: Media={'Media' in caps}, "
          f"Analytics={has_analytics}, Events={has_events}")

    scopes = onvif.get_scopes()
    profile_m = any("Profile/M" in s for s in scopes)
    print(f"  Profile M: {'FOUND' if profile_m else 'NOT FOUND'}")

    profiles = onvif.get_profiles()
    for p in profiles:
        v = p.get("video", {})
        print(f"  Profile: {p['token']} | {v.get('encoding','')} "
              f"{v.get('width','')}x{v.get('height','')} | "
              f"Metadata: {'Yes' if p.get('metadata',{}).get('present') else 'No'} | "
              f"Analytics: {'Yes' if p.get('analytics',{}).get('present') else 'No'}")

    # ── Determine RTSP URI ───────────────────────────────────────────
    rtsp_uri = args.rtsp_uri
    if not rtsp_uri and profiles:
        profile_token = profiles[0]["token"]
        rtsp_uri = onvif.get_stream_uri(profile_token)
        if rtsp_uri:
            print(f"  RTSP URI (discovered): {rtsp_uri}")
    if not rtsp_uri:
        rtsp_uri = f"rtsp://{args.camera_ip}:554/stream1"
        print(f"  RTSP URI (fallback): {rtsp_uri}")

    # ── RTSP probe ───────────────────────────────────────────────────
    print(f"\n[Discovery] RTSP Stream")
    print("-" * 70)
    stream_info = probe_rtsp(rtsp_uri)
    print(f"  Video: {stream_info.get('has_video', False)} | "
          f"Audio: {stream_info.get('has_audio', False)} | "
          f"Metadata: {stream_info.get('has_metadata', False)}")

    # ── DLStreamer VLM pipeline (lazy-start) ──────────────────────────
    print(f"\n[Setup] DLStreamer VLM pipeline (device: {args.device})")
    print("-" * 70)

    if not args.model_path:
        print("  FATAL: --model-path is required (or set GENAI_MODEL_PATH)")
        print("  Export a model first:")
        print("    optimum-cli export openvino --model openbmb/MiniCPM-V-2_6 "
              "--weight-format int4 MiniCPM-V-2_6")
        return

    print(f"  Model: {args.model_path}")
    print(f"  Device: {args.device}")
    print(f"  Prompt: {args.prompt}")
    print(f"  Frame rate: {args.frame_rate} fps")
    print(f"  Chunk size: {args.chunk_size}")
    print(f"  Max tokens: {args.max_tokens}")
    print(f"  Mode: lazy-start (idle timeout: {args.idle_timeout}s)")

    dls = DLStreamerPipeline(
        rtsp_uri=rtsp_uri,
        model_path=args.model_path,
        device=args.device,
        prompt=args.prompt,
        frame_rate=args.frame_rate,
        chunk_size=args.chunk_size,
        max_tokens=args.max_tokens,
        idle_timeout=args.idle_timeout,
    )
    if not dls.ensure_running():
        print("  FATAL: DLStreamer VLM pipeline failed to start")
        return
    print(f"  Pipeline: STARTED")

    # ── Start event listener (MQTT) ─────────────────────────────────
    print(f"\n[Setup] Event source: MQTT ({args.mqtt_broker}:{args.mqtt_port})")
    print("-" * 70)

    listener = MQTTEventListener(
        args.mqtt_broker, args.mqtt_port, args.mqtt_topics)
    if not listener.start():
        print("  FATAL: MQTT connection failed")
        return

    for _ in range(20):
        if listener.connected:
            break
        time.sleep(0.1)
    if not listener.connected:
        print("  FATAL: MQTT connection timeout")
        listener.stop()
        return

    print(f"  MQTT: CONNECTED (topics: {', '.join(args.mqtt_topics)})")

    # ── Start web UI ─────────────────────────────────────────────────
    web = start_web_ui(args.web_port, dls)
    print(f"\n[Setup] Web UI: http://localhost:{args.web_port}")

    # ==================================================================
    #  VALIDATION LOOP
    # ==================================================================
    print(f"\n{'=' * 70}")
    print(f"  Listening for MQTT events... (Ctrl+C to stop)")
    print(f"  Web UI: http://localhost:{args.web_port}")
    print(f"{'=' * 70}\n")

    stats = {
        "events_processed": 0,
        "events_validated": 0,
        "vlm_inferences": 0,
        "total_camera_objects": 0,
        "total_vlm_objects": 0,
        "mismatches": 0,
    }

    try:
        while True:
            try:
                event = listener.event_queue.get(timeout=1.0)
            except queue.Empty:
                stats["vlm_inferences"] = dls.get_vlm_count()
                DashboardHandler.stats = stats
                continue

            stats["events_processed"] += 1
            event_objects = event.get("objectCount", 0)
            event_classes = event.get("classCounts", {})

            # Ensure pipeline is running (restarts after idle timeout)
            if not dls.ensure_running():
                logging.error("DLStreamer pipeline failed to start, skipping")
                continue

            # Get VLM description and extract objects from text
            vlm_text = dls.get_vlm_result()
            vlm_detections = extract_objects_from_text(vlm_text)
            stats["vlm_inferences"] = dls.get_vlm_count()

            # Cross-validate camera event vs VLM description
            validation = cross_validate(event, vlm_detections)
            stats["total_camera_objects"] += validation["camera_objects"]
            stats["total_vlm_objects"] += validation["inference_objects"]
            if validation["needs_update"]:
                stats["mismatches"] += 1
            stats["events_validated"] += 1

            # Update web UI state
            DashboardHandler.latest_event = event
            DashboardHandler.latest_validation = validation
            DashboardHandler.latest_vlm_text = vlm_text
            DashboardHandler.stats = stats

            # Console output
            evt_n = stats["events_processed"]
            cam_str = ",".join(
                f"{k}:{v}" for k, v in event_classes.items())
            vlm_str = ",".join(
                f"{k}:{v}" for k, v in validation["inference_classes"].items())
            update_str = "MISMATCH" if validation["needs_update"] else "OK"
            vlm_short = (vlm_text[:60] + "...") if len(vlm_text) > 60 else vlm_text
            print(f"  [{evt_n:4d}] cam={event_objects}({cam_str}) "
                  f"vlm=({vlm_str}) -> {update_str}")
            if vlm_text:
                print(f"         VLM: {vlm_short}")

    except KeyboardInterrupt:
        print(f"\n\n{'=' * 70}")
        print(f"  VALIDATION STOPPED")
        print(f"{'=' * 70}")

    finally:
        dls.stop()
        listener.stop()

        print(f"\n  {'─' * 50}")
        print(f"  Event source:       MQTT")
        print(f"  Events processed:   {stats['events_processed']}")
        print(f"  Events validated:   {stats['events_validated']}")
        print(f"  VLM inferences:     {stats['vlm_inferences']}")
        print(f"  Camera objects:     {stats['total_camera_objects']}")
        print(f"  VLM objects:        {stats['total_vlm_objects']}")
        print(f"  Mismatches:         {stats['mismatches']}")
        print(f"  Events in:          {listener.stats['events_received']}")
        print(f"  Events dropped:     {listener.stats['events_dropped']}")
        print(f"{'=' * 70}\n")


def main():
    p = argparse.ArgumentParser(
        description="ONVIF Profile M VLM Analytics Validation — "
                    "works with any ONVIF-enabled camera",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Examples:
  %(prog)s --camera-ip 192.168.1.100 --model-path ./MiniCPM-V-2_6
  %(prog)s --camera-ip 192.168.1.100 --device GPU --model-path ./MiniCPM-V-2_6
  %(prog)s --rtsp-uri rtsp://cam:554/stream1 --model-path ./MiniCPM-V-2_6

Model export (run once):
  pip install optimum-intel openvino
  optimum-cli export openvino --model openbmb/MiniCPM-V-2_6 --weight-format int4 MiniCPM-V-2_6
""")
    p.add_argument("--camera-ip", default="192.168.1.100",
                   help="Camera IP address")
    p.add_argument("--onvif-port", type=int, default=80,
                   help="ONVIF HTTP port (default: 80)")
    p.add_argument("--onvif-user", default="admin")
    p.add_argument("--onvif-pass", default="admin")
    p.add_argument("--rtsp-uri", default="",
                   help="RTSP URI (auto-discovered from ONVIF if omitted)")
    p.add_argument("--model-path",
                   default=os.environ.get("GENAI_MODEL_PATH", ""),
                   help="Path to OpenVINO-exported VLM model directory "
                        "(or set GENAI_MODEL_PATH env var)")
    p.add_argument("--device", default="CPU",
                   choices=["CPU", "GPU", "NPU", "AUTO"],
                   help="Intel inference device (default: CPU)")
    p.add_argument("--prompt",
                   default="Describe only the objects you can clearly see in "
                           "this image. State the count of each object type. "
                           "Do not guess or assume objects that are not visible.",
                   help="VLM prompt for scene description")
    p.add_argument("--frame-rate", type=int, default=1,
                   help="VLM frame sampling rate in fps (default: 1)")
    p.add_argument("--chunk-size", type=int, default=1,
                   help="Frames per VLM inference call (default: 1)")
    p.add_argument("--max-tokens", type=int, default=150,
                   help="Max tokens for VLM generation (default: 150)")
    p.add_argument("--idle-timeout", type=int, default=60,
                   help="Stop pipeline after N seconds idle (default: 60)")
    p.add_argument("--mqtt-broker", default="localhost",
                   help="MQTT broker address")
    p.add_argument("--mqtt-port", type=int, default=1883,
                   help="MQTT broker port")
    p.add_argument("--mqtt-topics", nargs="+",
                   default=["onvif/analytics", "onvif/analytics/events"],
                   help="MQTT topics to subscribe to")
    p.add_argument("--web-port", type=int, default=8080,
                   help="Web dashboard port (default: 8080)")
    args = p.parse_args()
    run(args)


if __name__ == "__main__":
    main()
