# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

## @file region_of_interest.py
#  @brief This file contains gstgva.region_of_interest.RegionOfInterest class to control region of interest for particular gstgva.video_frame.VideoFrame with gstgva.tensor.Tensor instances attached

import ctypes
import numpy
from typing import List
from collections import namedtuple

from .tensor import Tensor
from .util import VideoRegionOfInterestMeta, GstStructureHandle
from .util import libgst, libgobject, libgstvideo, GLIST_POINTER

import gi

gi.require_version("GstVideo", "1.0")
gi.require_version("GLib", "2.0")
gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
from gi.repository import GstVideo, GLib, GObject, Gst, GstAnalytics

Rect = namedtuple("Rect", "x y w h")


## @brief This class represents region of interest - object describing detection result (bounding box) and containing
# multiple Tensor objects (inference results) attached by multiple models. For example, it can be region of interest with detected face and recognized age
# and sex of a person. It can be produced by a pipeline with gvadetect with detection model and two gvaclassify
# elements with two classification models. Such RegionOfInterest will have bounding box coordinates filled and will have 3 Tensor objects
# attached - 1 Tensor object with detection result and 2 Tensor objects with classification results coming from 2 classifications
class RegionOfInterest(object):

    def __init__(
        self, od_meta: GstAnalytics.ODMtd, roi_meta: VideoRegionOfInterestMeta
    ):
        self.__roi_meta = roi_meta
        self.__od_meta = od_meta
        self._detection = None
        self._tensors = []
        self._converted_structures: list[GstStructureHandle] = []

        # Load existing tensors from ROI meta
        if roi_meta and roi_meta._params:
            param = roi_meta._params
            while param:
                tensor_structure = param.contents.data
                tensor = Tensor(tensor_structure)
                if (
                    tensor.name() != "object_id"
                    and tensor.type() != "classification_result"
                ):
                    self._tensors.append(tensor)
                    if tensor.is_detection():
                        self._detection = tensor
                param = param.contents.next

        for rlt_mtd in od_meta.meta:
            if rlt_mtd.id == od_meta.id:
                continue

            rel = od_meta.meta.get_relation(od_meta.id, rlt_mtd.id)

            if rel != GstAnalytics.RelTypes.CONTAIN:
                continue

            tensor_structure = Tensor.convert_to_tensor(rlt_mtd)
            if tensor_structure is not None:
                tensor = Tensor(tensor_structure)
                self._tensors.append(tensor)
                handle = GstStructureHandle(tensor_structure)
                self._converted_structures.append(handle)

    ## @brief Get bounding box of the RegionOfInterest as pixel coordinates in original image
    #  @return Bounding box coordinates of the RegionOfInterest
    def rect(self) -> Rect:
        success, x, y, w, h, _, _ = self.__od_meta.get_oriented_location()

        if not success:
            raise RuntimeError(
                "RegionOfInterest:rect: Failed to get oriented location from analytics metadata"
            )

        return Rect(x=x, y=y, w=w, h=h)

    ## @brief Get bounding box of the RegionOfInterest as normalized coordinates in the range [0, 1]
    #  @return Bounding box coordinates of the RegionOfInterest
    def normalized_rect(self):
        detection = self.detection()
        return Rect(
            x=detection["x_min"],
            y=detection["y_min"],
            w=detection["x_max"] - detection["x_min"],
            h=detection["y_max"] - detection["y_min"],
        )

    ## @brief Get bounding box rotation
    #  @return Bounding box rotation of the RegionOfInterest
    def rotation(self) -> float:
        """Get RegionOfInterest bounding box rotation using analytics metadata."""
        success, _, _, _, _, r, _ = self.__od_meta.get_oriented_location()

        if not success:
            raise RuntimeError(
                "RegionOfInterest:rotation: Failed to get oriented location from analytics metadata"
            )

        if r >= 0.0:
            return r

        return 0.0

    ## @brief Get class label of this RegionOfInterest
    #  @return Class label of this RegionOfInterest
    def label(self) -> str:
        label_quark = self.__od_meta.get_obj_type()

        if label_quark:
            return GLib.quark_to_string(label_quark)

        return ""

    ## @brief Get RegionOfInterest detection confidence (set by gvadetect)
    # @return detection confidence from analytics metadata
    # @throws std::runtime_error if confidence cannot be read from metadata
    def confidence(self) -> float:
        success, confidence = self.__od_meta.get_confidence_lvl()

        if not success:
            raise RuntimeError(
                "RegionOfInterest:confidence: Failed to get confidence level from analytics metadata"
            )

        return confidence

    ## @brief Get object id using analytics tracking metadata
    # @return object id as an int, None if failed to get
    def object_id(self) -> int | None:
        for trk_mtd in self.__od_meta.meta:
            if (
                trk_mtd.id == self.__od_meta.id
                or type(trk_mtd) != GstAnalytics.TrackingMtd
            ):
                continue

            rel = self.__od_meta.meta.get_relation(self.__od_meta.id, trk_mtd.id)

            if rel == GstAnalytics.RelTypes.NONE:
                continue

            success, tracking_id, _, _, _ = trk_mtd.get_info()

            if not success:
                raise RuntimeError(
                    "RegionOfInterest:object_id: Failed to get tracking info from analytics metadata"
                )

            return tracking_id

        return None

    ## @brief Set object id using analytics tracking metadata
    # @param object_id Object ID to set
    def set_object_id(self, object_id: int):
        # Set in ROI meta for backward compatibility
        if self.meta():
            s_object_id = libgstvideo.gst_video_region_of_interest_meta_get_param(
                self.meta(), "object_id".encode("utf-8")
            )

            if s_object_id:
                tensor_object_id = Tensor(s_object_id)
                tensor_object_id["id"] = object_id
            else:
                tensor_structure = libgst.gst_structure_new_empty(
                    "object_id".encode("utf-8")
                )
                tensor_object_id = Tensor(tensor_structure)
                tensor_object_id["id"] = object_id
                libgstvideo.gst_video_region_of_interest_meta_add_param(
                    self.meta(), tensor_structure
                )

        for trk_mtd in self.__od_meta.meta:
            if (
                trk_mtd.id == self.__od_meta.id
                or type(trk_mtd) != GstAnalytics.TrackingMtd
            ):
                continue

            rel = self.__od_meta.meta.get_relation(self.__od_meta.id, trk_mtd.id)

            if rel == GstAnalytics.RelTypes.NONE:
                continue

            if not self.__od_meta.meta.set_relation(
                GstAnalytics.RelTypes.NONE, self.__od_meta.id, trk_mtd.id
            ):
                raise RuntimeError(
                    "RegionOfInterest:set_object_id: Failed to remove existing relation to tracking metadata"
                )

        success, trk_mtd = self.__od_meta.meta.add_tracking_mtd(object_id, 0)

        if not success:
            raise RuntimeError(
                "RegionOfInterest:set_object_id: Failed to add tracking metadata"
            )

        if not self.__od_meta.meta.set_relation(
            GstAnalytics.RelTypes.RELATE_TO, self.__od_meta.id, trk_mtd.id
        ):
            raise RuntimeError(
                "RegionOfInterest:set_object_id: Failed to set relation to tracking metadata"
            )

    ## @brief Get all Tensor instances added to this RegionOfInterest
    # @return list of Tensor instances added to this RegionOfInterest
    def tensors(self) -> List[Tensor]:
        return self._tensors

    ## @brief Get first tensor matching the given name
    # @param name Tensor name to search for
    # @return Tensor instance with matching name, None if not found
    def get_tensor_by_name(self, name: str) -> Tensor | None:
        for tensor in self.tensors():
            if tensor.name() == name:
                return tensor
        return None

    ## @brief Get all Tensor instances added to this RegionOfInterest (in old metadata format)
    # @return list of Tensor instances added to this RegionOfInterest
    def get_gst_roi_params(self) -> List[Tensor]:
        result = []

        param = self.__roi_meta._params
        while param:
            tensor_structure = param.contents.data
            tensor = Tensor(tensor_structure)
            result.append(tensor)
            param = param.contents.next

        return result

    ## @brief Returns detection Tensor, last added to this RegionOfInterest. As any other Tensor, returned detection
    # Tensor can contain arbitrary information. If you use RegionOfInterest based on VideoRegionOfInterestMeta
    # attached by gvadetect by default, then this Tensor will contain "label_id", "confidence", "x_min", "x_max",
    # "y_min", "y_max" fields.
    # If RegionOfInterest doesn't have detection Tensor, it will be created in-place
    # @return detection Tensor, empty if there were no detection Tensor objects added to this RegionOfInterest when
    # this method was called
    def detection(self) -> Tensor:
        if not self._detection:
            gst_structure = libgst.gst_structure_new_empty("detection".encode("utf-8"))
            detection_tensor = Tensor(gst_structure)
            self.add_tensor(detection_tensor)
        return self._detection

    ## @brief Get label_id from analytics metadata or detection Tensor
    # @return label_id if exists, otherwise 0
    def label_id(self) -> int:
        label_quark = self.__od_meta.get_obj_type()

        cls_descriptor_mtd = None
        for cls_descriptor_mtd in self.__od_meta.meta:
            if (
                cls_descriptor_mtd.id == self.__od_meta.id
                or type(cls_descriptor_mtd) != GstAnalytics.ClsMtd
            ):
                continue

            rel = self.__od_meta.meta.get_relation(
                self.__od_meta.id, cls_descriptor_mtd.id
            )

            if rel == GstAnalytics.RelTypes.RELATE_TO:
                break

            cls_descriptor_mtd = None

        if label_quark and cls_descriptor_mtd is not None:
            label_id = cls_descriptor_mtd.get_index_by_quark(label_quark)
            if label_id < 0:
                raise RuntimeError(
                    "RegionOfInterest:label_id: Failed to get label id from analytics metadata"
                )
            return label_id

        return 0

    ## @brief Add new Tensor (inference result) to the RegionOfInterest.
    # @param tensor Tensor object to add to this RegionOfInterest.
    # This function does not take ownership of tensor passed, but only copies its contents
    def add_tensor(self, tensor: Tensor):
        s = tensor.get_structure()

        if not s:
            raise ValueError("RegionOfInterest:add_tensor: Invalid tensor structure")
        libgstvideo.gst_video_region_of_interest_meta_add_param(self.meta(), s)

        tensor_mtd = tensor.convert_to_meta(self.__od_meta.meta)
        if tensor_mtd is not None:
            if not self.__od_meta.meta.set_relation(
                GstAnalytics.RelTypes.CONTAIN, self.__od_meta.id, tensor_mtd.id
            ):
                raise RuntimeError(
                    "RegionOfInterest:add_tensor: Failed to set relation to tensor metadata"
                )

            if not self.__od_meta.meta.set_relation(
                GstAnalytics.RelTypes.IS_PART_OF, tensor_mtd.id, self.__od_meta.id
            ):
                raise RuntimeError(
                    "RegionOfInterest:add_tensor: Failed to set reverse relation to tensor metadata"
                )

        self._tensors.append(tensor)
        if tensor.is_detection():
            self._detection = tensor

    ## @brief Get VideoRegionOfInterestMeta containing bounding box information and tensors (inference results).
    # Tensors are represented as GstStructures added to GstVideoRegionOfInterestMeta.params
    # @return VideoRegionOfInterestMeta containing bounding box and tensors (inference results)
    def meta(self) -> VideoRegionOfInterestMeta:
        return self.__roi_meta

    ## @brief Get region ID from analytics metadata
    # @return Region id as an int. Can be a positive or negative integer, but never zero.
    def region_id(self):
        return self.__od_meta.id

    ## @brief Retrieves the parent object detection ID for this region of interest.
    # @return The ID of the parent object detection metadata if found, None otherwise.
    def parent_id(self) -> int | None:
        for rlt_mtd in self.__od_meta.meta:
            if rlt_mtd.id == self.__od_meta.id or type(rlt_mtd) != GstAnalytics.ODMtd:
                continue

            rel = self.__od_meta.meta.get_relation(self.__od_meta.id, rlt_mtd.id)

            if rel != GstAnalytics.RelTypes.IS_PART_OF:
                continue

            return rlt_mtd.id

        return None

    ## @brief Iterate by VideoRegionOfInterestMeta instances attached to buffer
    # @param buffer buffer with GstVideoRegionOfInterestMeta instances attached
    # @return generator for VideoRegionOfInterestMeta instances attached to buffer
    @classmethod
    def _iterate(cls, buffer: Gst.Buffer):
        relation_meta = GstAnalytics.buffer_get_analytics_relation_meta(buffer)

        if relation_meta is None:
            return

        for od_mtd in relation_meta:
            if type(od_mtd) != GstAnalytics.ODMtd:
                continue

            value = None
            value = libgstvideo.gst_buffer_get_video_region_of_interest_meta_id(
                hash(buffer), od_mtd.id
            )

            if not value:
                raise RuntimeError(
                    "RegionOfInterest:_iterate: Failed to get VideoRegionOfInterestMeta by id from buffer"
                )

            roi_meta = ctypes.cast(
                value, ctypes.POINTER(VideoRegionOfInterestMeta)
            ).contents

            yield RegionOfInterest(od_mtd, roi_meta)
