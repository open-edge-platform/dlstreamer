#!/bin/bash
# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

# Smoke test of the installed DL Streamer RPMs. No GPU, network or models are needed.

set -euo pipefail

WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

echo "=== Plugins load without warnings and provide the elements"
export GST_REGISTRY="$WORKDIR/registry.bin"
GST_DEBUG=1 gst-inspect-1.0 >"$WORKDIR/inspect.log" 2>&1
if grep -E "WARN|ERROR|cannot open shared object" "$WORKDIR/inspect.log"; then
    echo "Plugin loading reported problems" >&2
    exit 1
fi
for element in gvadetect gvaclassify gvainference gvametaconvert gvametaaggregate gvawatermark gvatrack \
    gvafpscounter gvapython gvametapublish gvametapublishfile gvametapublishkafka gvametapublishmqtt \
    gvastreammux gvastreamdemux g3dlidarparse; do
    gst-inspect-1.0 --exists "$element"
    echo "ok: $element"
done
rpm -q --whatprovides "gstreamer1(element-gvadetect)()(64bit)" "gstreamer1(element-gvametapublishkafka)()(64bit)"

echo "=== Pipeline without models: fps counter and watermark"
gst-launch-1.0 -q videotestsrc num-buffers=30 ! videoconvert ! gvafpscounter ! gvawatermark ! fakesink

echo "=== Python: typelibs and keypoint descriptors from the vendored metadata"
python3 - <<'EOF'
import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
gi.require_version("DLStreamerMeta", "1.0")
gi.require_version("DLStreamerWatermarkMeta", "1.0")
from gi.repository import DLStreamerMeta, DLStreamerWatermarkMeta  # noqa: E402,F401

import gstgva  # noqa: E402,F401

descriptor = DLStreamerMeta.KeypointDescriptor.lookup("body-pose/coco-17")
assert descriptor is not None and descriptor.point_count == 17
print("ok: DLStreamerMeta.KeypointDescriptor")
EOF

echo "=== Pipeline with gvapython: a region is added, iterated and published as JSON"
cat >"$WORKDIR/add_region.py" <<'EOF'
from gstgva import VideoFrame


def process_frame(frame: VideoFrame) -> bool:
    frame.add_region(10, 20, 100, 80, "smoke", 0.9)
    assert [region.label() for region in frame.regions()] == ["smoke"]
    return True
EOF
gst-launch-1.0 -q videotestsrc num-buffers=3 ! video/x-raw,format=BGR,width=320,height=240 ! \
    gvapython module="$WORKDIR/add_region.py" function=process_frame ! \
    gvametaconvert format=json ! \
    gvametapublish method=file file-format=json-lines file-path="$WORKDIR/meta.json" ! fakesink
python3 - "$WORKDIR/meta.json" <<'EOF'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as published:
    frames = [json.loads(line) for line in published if line.strip()]
assert len(frames) == 3, frames
assert all(frame["objects"][0]["detection"]["label"] == "smoke" for frame in frames), frames
print("ok: 3 frames published with the region")
EOF

echo "=== Development files"
pkg-config --exists dl-streamer
test -f /usr/include/dlstreamer/gst/metadata/gstanalyticskeypointmtd.h
test -f /usr/share/gir-1.0/DLStreamerMeta-1.0.gir

echo "=== Installed files are intact"
rpm -V dlstreamer dlstreamer-devel python3-dlstreamer dlstreamer-samples

echo "Smoke test passed"
