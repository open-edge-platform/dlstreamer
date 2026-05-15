# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
Custom GStreamer element: crop a single worker region from the frame.

Reads the __crop__ tag placed by gvaWorkerSelection, extracts the bounding box
region with margin, and scales it to the configured output resolution suitable
for the VLM model. Preserves aspect ratio with letterboxing.
"""

import numpy as np

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstBase", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import Gst, GstBase, GObject, GLib, GstAnalytics

Gst.init_python()


class WorkerCrop(GstBase.BaseTransform):
    """Crop a tagged worker region and scale to VLM input resolution."""

    __gstmetadata__ = (
        "GVA Worker Crop",
        "Transform",
        "Crops tagged worker region from frame with margin and letterboxing",
        "Intel DLStreamer",
    )

    __gsttemplates__ = (
        Gst.PadTemplate.new(
            "src", Gst.PadDirection.SRC, Gst.PadPresence.ALWAYS, Gst.Caps.new_any()
        ),
        Gst.PadTemplate.new(
            "sink", Gst.PadDirection.SINK, Gst.PadPresence.ALWAYS, Gst.Caps.new_any()
        ),
    )

    # Square crop — Qwen2.5-VL tile size = 28, full tile = 448
    _out_width = 448
    _out_height = 448
    _margin = 0.20  # 20% margin around bounding box

    @GObject.Property(type=int, nick="out-width", blurb="Output crop width")
    def out_width(self):
        return self._out_width

    @out_width.setter
    def out_width(self, value):
        self._out_width = value

    @GObject.Property(type=int, nick="out-height", blurb="Output crop height")
    def out_height(self):
        return self._out_height

    @out_height.setter
    def out_height(self, value):
        self._out_height = value

    @GObject.Property(type=float, nick="margin", blurb="Margin ratio around bbox")
    def margin(self):
        return self._margin

    @margin.setter
    def margin(self, value):
        self._margin = value

    def do_transform_caps(self, direction, caps, filter_caps):
        if direction == Gst.PadDirection.SINK:
            out = Gst.Caps.from_string(
                f"video/x-raw,format=RGB,width={self._out_width},height={self._out_height}"
            )
        else:
            out = Gst.Caps.new_any()
        return out.intersect(filter_caps) if filter_caps else out

    def do_fixate_caps(self, direction, caps, othercaps):
        if direction == Gst.PadDirection.SINK:
            return Gst.Caps.from_string(
                f"video/x-raw,format=RGB,width={self._out_width},height={self._out_height}"
            ).fixate()
        return othercaps.fixate()

    def do_prepare_output_buffer(self, inbuf):
        # Find the __crop__ tag in metadata
        rmeta = GstAnalytics.buffer_get_analytics_relation_meta(inbuf)
        if not rmeta:
            return Gst.FlowReturn.OK, inbuf

        crop_bbox = None
        for mtd in rmeta:
            if not isinstance(mtd, GstAnalytics.ODMtd):
                continue
            label = GLib.quark_to_string(mtd.get_obj_type())
            if label and label.startswith("__crop__:"):
                parts = label.split(":")[1].split(",")
                crop_bbox = tuple(int(p) for p in parts)
                break

        if crop_bbox is None:
            return Gst.FlowReturn.OK, inbuf

        # Get input frame dimensions from caps
        caps = self.sinkpad.get_current_caps()
        if not caps:
            return Gst.FlowReturn.OK, inbuf

        structure = caps.get_structure(0)
        _, in_w = structure.get_int("width")
        _, in_h = structure.get_int("height")
        fmt = structure.get_string("format")

        success, map_info = inbuf.map(Gst.MapFlags.READ)
        if not success:
            return Gst.FlowReturn.OK, inbuf

        try:
            if fmt == "RGB":
                channels = 3
            elif fmt == "RGBA":
                channels = 4
            elif fmt == "BGR":
                channels = 3
            else:
                channels = 3

            frame = np.ndarray((in_h, in_w, channels), dtype=np.uint8, buffer=map_info.data)

            bx, by, bw, bh = crop_bbox
            # Add margin
            margin_x = int(bw * self._margin)
            margin_y = int(bh * self._margin)
            x1 = max(0, bx - margin_x)
            y1 = max(0, by - margin_y)
            x2 = min(in_w, bx + bw + margin_x)
            y2 = min(in_h, by + bh + margin_y)

            crop = frame[y1:y2, x1:x2]

            # Resize with letterboxing to preserve aspect ratio
            crop_h, crop_w = crop.shape[:2]
            if crop_h == 0 or crop_w == 0:
                result = np.zeros((self._out_height, self._out_width, 3), dtype=np.uint8)
            else:
                scale = min(self._out_width / crop_w, self._out_height / crop_h)
                new_w = int(crop_w * scale)
                new_h = int(crop_h * scale)

                # Simple nearest-neighbor resize (no OpenCV dependency)
                row_idx = (np.arange(new_h) * crop_h / new_h).astype(int)
                col_idx = (np.arange(new_w) * crop_w / new_w).astype(int)
                resized = crop[row_idx][:, col_idx]

                # Letterbox: place centered on black background
                result = np.zeros((self._out_height, self._out_width, 3), dtype=np.uint8)
                y_off = (self._out_height - new_h) // 2
                x_off = (self._out_width - new_w) // 2
                result[y_off : y_off + new_h, x_off : x_off + new_w] = resized[:, :, :3]

        finally:
            inbuf.unmap(map_info)

        outbuf = Gst.Buffer.new_wrapped(result.tobytes())
        outbuf.pts = inbuf.pts
        outbuf.dts = inbuf.dts
        outbuf.duration = inbuf.duration

        # Copy non-crop metadata to output buffer
        in_rmeta = GstAnalytics.buffer_get_analytics_relation_meta(inbuf)
        if in_rmeta:
            out_rmeta = GstAnalytics.buffer_add_analytics_relation_meta(outbuf)
            for mtd in in_rmeta:
                if isinstance(mtd, GstAnalytics.ODMtd):
                    label = GLib.quark_to_string(mtd.get_obj_type())
                    if label and not label.startswith("__crop__"):
                        _, conf = mtd.get_confidence_lvl()
                        out_rmeta.add_od_mtd(mtd.get_obj_type(), 0, 0, 0, 0, conf)

        return Gst.FlowReturn.OK, outbuf

    def do_transform_ip(self, buffer):
        return Gst.FlowReturn.OK  # no-op — all work done in do_prepare_output_buffer


GObject.type_register(WorkerCrop)
__gstelementfactory__ = ("gvaworkercrop_py", Gst.Rank.NONE, WorkerCrop)
