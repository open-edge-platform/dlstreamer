# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

## @file video_frame.py
#  @brief This file contains gstgva.video_frame.VideoFrame class to control particular inferenced frame
# and attached gstgva.region_of_interest.RegionOfInterest and gstgva.tensor.Tensor instances

import ctypes
import numpy
from contextlib import contextmanager
from typing import List
from warnings import warn
import json

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstVideo", "1.0")
gi.require_version("GObject", "2.0")
gi.require_version("GstAnalytics", "1.0")
gi.require_version("GLib", "2.0")

from gi.repository import Gst, GstVideo, GstAnalytics, GLib
from .util import VideoRegionOfInterestMeta
from .util import GVATensorMeta
from .util import GVAJSONMeta
from .util import GVAJSONMetaStr
from .region_of_interest import RegionOfInterest
from .tensor import Tensor
from .util import libgst, gst_buffer_data, VideoInfoFromCaps


## @brief This class represents video frame - object for working with RegionOfInterest and Tensor objects which
# belong to this video frame (image). RegionOfInterest describes detected object (bounding boxes) and its Tensor
# objects (inference results on RegionOfInterest level). Tensor describes inference results on VideoFrame level.
# VideoFrame also provides access to underlying GstBuffer and GstVideoInfo describing frame's video information (such
# as image width, height, channels, strides, etc.). You also can get cv::Mat object representing this video frame.
class VideoFrame:
    ## @brief Construct VideoFrame instance from Gst.Buffer and GstVideo.VideoInfo or Gst.Caps.
    #  The preferred way of creating VideoFrame is to use Gst.Buffer and GstVideo.VideoInfo
    #  @param buffer Gst.Buffer to which metadata is attached and retrieved
    #  @param video_info GstVideo.VideoInfo containing video information
    #  @param caps Gst.Caps from which video information is obtained
    def __init__(
        self,
        buffer: Gst.Buffer,
        video_info: GstVideo.VideoInfo = None,
        caps: Gst.Caps = None,
    ):
        self.__buffer = buffer
        self.__video_info = None

        if video_info:
            self.__video_info = video_info
        elif caps:
            self.__video_info = VideoInfoFromCaps(caps)
        elif self.video_meta():
            # Check for GST 1.20 API
            if hasattr(GstVideo.VideoInfo, "new"):
                self.__video_info = GstVideo.VideoInfo.new()
            else:
                self.__video_info = GstVideo.VideoInfo()
            self.__video_info.width = self.video_meta().width
            self.__video_info.height = self.video_meta().height

    ## @brief Get video metadata of buffer
    #  @return GstVideo.VideoMeta of buffer, nullptr if no GstVideo.VideoMeta available
    def video_meta(self) -> GstVideo.VideoMeta:
        return GstVideo.buffer_get_video_meta(self.__buffer)

    ## @brief Get GstVideo.VideoInfo of this VideoFrame. This is preferrable way of getting any image information
    #  @return GstVideo.VideoInfo of this VideoFrame
    def video_info(self) -> GstVideo.VideoInfo:
        return self.__video_info

    ## @brief Get RegionOfInterest objects attached to VideoFrame
    #  @return iterator of RegionOfInterest objects attached to VideoFrame
    def regions(self):
        return RegionOfInterest._iterate(self.__buffer)

    ## @brief Get Tensor objects attached to VideoFrame
    #  @return iterator of Tensor objects attached to VideoFrame
    def tensors(self):
        return Tensor._iterate(self.__buffer)

    ## @brief Attach RegionOfInterest to this VideoFrame
    #  @param x x coordinate of the upper left corner of bounding box
    #  @param y y coordinate of the upper left corner of bounding box
    #  @param w bounding box width
    #  @param h bounding box height
    #  @param label object label
    #  @param confidence detection confidence
    #  @param region_tensor base tensor for detection Tensor which will be added to this new
    #  @param normalized if True, input coordinates are assumed to be normalized (in [0,1] interval).
    # If False, input coordinates are assumed to be expressed in pixels (this is behavior by default)
    #  @return new RegionOfInterest instance

    def add_region(
        self,
        x,
        y,
        w,
        h,
        label: str = "",
        confidence: float = 0.0,
        normalized: bool = False,
        extra_params=None,
    ) -> RegionOfInterest:
        if normalized:
            x = int(x * self.video_info().width)
            y = int(y * self.video_info().height)
            w = int(w * self.video_info().width)
            h = int(h * self.video_info().height)

        if not self.__is_bounded(x, y, w, h):
            x_init, y_init, w_init, h_init = x, y, w, h
            x, y, w, h = self.__clip(x, y, w, h)
            warn(
                "ROI coordinates [x, y, w, h] are out of image borders and will be clipped: [{}, {}, {}, {}] -> "
                "[{}, {}, {}, {}]".format(x_init, y_init, w_init, h_init, x, y, w, h),
                stacklevel=2,
            )

        relation_meta = GstAnalytics.buffer_add_analytics_relation_meta(self.__buffer)

        if not relation_meta:
            raise RuntimeError(
                "VideoFrame:add_region: Failed to add GstAnalyticsRelationMeta to buffer"
            )

        label_quark = GLib.quark_from_string(label) if label else 0
        success, od_mtd = relation_meta.add_oriented_od_mtd(
            label_quark, x, y, w, h, 0.0, float(confidence)
        )

        if not success:
            raise RuntimeError(
                "VideoFrame:add_region: Failed to add OrientedODMeta to GstAnalyticsRelationMeta"
            )

        video_roi_meta = GstVideo.buffer_add_video_region_of_interest_meta(
            self.__buffer, label, x, y, w, h
        )
        video_roi_meta.id = od_mtd.id
        roi = RegionOfInterest(
            od_mtd,
            ctypes.cast(
                hash(video_roi_meta), ctypes.POINTER(VideoRegionOfInterestMeta)
            ).contents,
        )

        tensor_structure = libgst.gst_structure_new_empty("detection".encode("utf-8"))
        tensor = Tensor(tensor_structure)
        tensor["confidence"] = float(confidence)
        tensor["x_min"] = float(x / self.video_info().width)
        tensor["x_max"] = float((x + w) / self.video_info().width)
        tensor["y_min"] = float(y / self.video_info().height)
        tensor["y_max"] = float((y + h) / self.video_info().height)
        roi.add_tensor(tensor)

        # Add additional parameters if provided
        if extra_params is not None:
            # Serialize as JSON and store as a string field
            tensor["extra_params_json"] = json.dumps(extra_params)

        return roi

    ## @brief Attach empty Tensor to this VideoFrame
    #  @return new Tensor instance
    def add_tensor(self) -> Tensor:
        tensor_meta = GVATensorMeta.add_tensor_meta(self.__buffer)
        if tensor_meta:
            return Tensor(tensor_meta.data)
        return None

    ## @brief Get messages attached to this VideoFrame
    #  @return messages attached to this VideoFrame
    def messages(self) -> List[str]:
        return [
            json_meta.get_message() for json_meta in GVAJSONMeta.iterate(self.__buffer)
        ]

    ## @brief Attach message to this VideoFrame
    #  @param message message to attach to this VideoFrame
    def add_message(self, message: str):
        GVAJSONMeta.add_json_meta(self.__buffer, message)

    ## @brief Remove message from this VideoFrame
    #  @param message message to remove
    def remove_message(self, message: str):
        if not isinstance(message, GVAJSONMetaStr) or not GVAJSONMeta.remove_json_meta(
            self.__buffer, message.meta
        ):
            raise RuntimeError("VideoFrame: message doesn't belong to this VideoFrame")

    ## @brief Remove region with the specified index
    #  @param roi Region to remove
    def remove_region(self, roi) -> None:
        if not libgst.gst_buffer_remove_meta(
            hash(self.__buffer), ctypes.byref(roi.meta())
        ):
            raise RuntimeError(
                "VideoFrame: Underlying GstVideoRegionOfInterestMeta for RegionOfInterest "
                "doesn't belong to this VideoFrame"
            )

    ## @brief Get buffer data wrapped by numpy.ndarray
    #  @return numpy array instance
    @contextmanager
    def data(self, flag: Gst.MapFlags = Gst.MapFlags.READ) -> numpy.ndarray:
        with gst_buffer_data(self.__buffer, flag) as data:
            # pixel stride for 1st plane. works well for for 1-plane formats, like BGR, BGRA, BGRx
            bytes_per_pix = self.__video_info.finfo.pixel_stride[0]
            is_yuv_format = self.__video_info.finfo.format in [
                GstVideo.VideoFormat.NV12,
                GstVideo.VideoFormat.I420,
            ]
            w = self.__video_info.width
            if is_yuv_format:
                h = int(self.__video_info.height * 1.5)
            elif self.__video_info.finfo.format in [
                GstVideo.VideoFormat.BGR,
                GstVideo.VideoFormat.BGRA,
                GstVideo.VideoFormat.BGRX,
            ]:
                h = self.__video_info.height
            else:
                raise RuntimeError("VideoFrame.data: Unsupported format")

            mapped_data_size = len(data)
            requested_size = h * w * bytes_per_pix

            if mapped_data_size != requested_size:
                warn(
                    "Size of buffer's data: {}, and requested size: {}\n"
                    "Let to get shape from video meta or repack video frame...".format(
                        mapped_data_size, requested_size
                    ),
                    stacklevel=2,
                )
                meta = self.video_meta()
                if meta:
                    h, w = meta.height, meta.width
                    requested_size = h * w * bytes_per_pix

                else:
                    warn(
                        "Video meta is {}. Can't get shape.".format(meta), stacklevel=2
                    )

            try:
                if mapped_data_size < requested_size:
                    raise RuntimeError("VideoFrame.data: Corrupted buffer")
                elif mapped_data_size == requested_size:
                    yield numpy.ndarray(
                        (h, w, bytes_per_pix), buffer=data, dtype=numpy.uint8
                    )
                elif is_yuv_format:
                    # In some cases image size after mapping can be larger than expected image size.
                    # One of the reasons can be VA-API decoder appends padding to the end of each
                    # plane, so the height / width is multiple of a specific value (depends on
                    # hardware architecture). We need to return an image that has the same
                    # resolution as in video_info by dropping extra padding added by decoder.
                    yield self.__repack_video_frame(data)
                else:
                    raise RuntimeError("VideoFrame.data: Corrupted buffer")
            except TypeError as e:
                warn(
                    str(e)
                    + f"\nSize of buffer's data: {mapped_data_size}, and requested size: {requested_size}",
                    stacklevel=2,
                )
                raise e

    def __is_bounded(self, x, y, w, h):
        return (
            x >= 0
            and y >= 0
            and w >= 0
            and h >= 0
            and x + w <= self.__video_info.width
            and y + h <= self.__video_info.height
        )

    def __clip(self, x, y, w, h):
        frame_width, frame_height = self.__video_info.width, self.__video_info.height

        x, y = min(max(x, 0), frame_width), min(max(y, 0), frame_height)

        w, h = max(w, 0), max(h, 0)
        w = (frame_width - x) if (w + x) > frame_width else w
        h = (frame_height - y) if (h + y) > frame_height else h

        return x, y, w, h

    def __repack_video_frame(self, data):
        meta = self.video_meta()
        if not meta:
            raise RuntimeError(
                "VideoFrame.__repack_video_frame: No video meta available"
            )

        n_planes = meta.n_planes
        if n_planes not in [2, 3]:
            raise RuntimeError(
                f"VideoFrame.__repack_video_frame: Unsupported number of planes {n_planes}"
            )

        h, w = self.__video_info.height, self.__video_info.width
        bytes_per_pix = self.__video_info.finfo.pixel_stride[0]

        data_flat = numpy.frombuffer(data, dtype=numpy.uint8)

        # Y plane
        y_stride = meta.stride[0]
        y_offset = meta.offset[0]

        y_plane = numpy.lib.stride_tricks.as_strided(
            data_flat[y_offset:],
            shape=(h, w, bytes_per_pix),
            strides=(y_stride, bytes_per_pix, 1)
        ).copy()

        planes = [y_plane]

        if n_planes == 2:
            # NV12 format: interleaved UV plane
            uv_stride = meta.stride[1]
            uv_offset = meta.offset[1]
            uv_h = h // 2

            uv_plane = numpy.lib.stride_tricks.as_strided(
                data_flat[uv_offset:],
                shape=(uv_h, w, bytes_per_pix),
                strides=(uv_stride, bytes_per_pix, 1)
            ).copy()
            planes.append(uv_plane)
        else:
            # I420 format: separate U and V planes
            u_stride = meta.stride[1]
            v_stride = meta.stride[2]
            u_offset = meta.offset[1]
            v_offset = meta.offset[2]

            uv_h = h // 4
            uv_w = w

            u_plane = numpy.lib.stride_tricks.as_strided(
                data_flat[u_offset:],
                shape=(uv_h, uv_w, bytes_per_pix),
                strides=(u_stride, bytes_per_pix, 1)
            ).copy()

            v_plane = numpy.lib.stride_tricks.as_strided(
                data_flat[v_offset:],
                shape=(uv_h, uv_w, bytes_per_pix),
                strides=(v_stride, bytes_per_pix, 1)
            ).copy()

            planes.append(u_plane)
            planes.append(v_plane)

        return numpy.concatenate(planes)

    @staticmethod
    def __get_label_by_label_id(region_tensor: Gst.Structure, label_id: int) -> str:
        if region_tensor and region_tensor.has_field("labels"):
            res = region_tensor.get_array("labels")
            if res[0] and 0 <= label_id < res[1].n_values:
                return res[1].get_nth(label_id)
        return ""
