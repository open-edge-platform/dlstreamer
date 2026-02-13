#!/usr/bin/python3

# ==============================================================================
# Copyright (C) 2023-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstBase', '1.0')
gi.require_version('GstVideo', '1.0')
from gi.repository import Gst, GObject, GstBase, GLib # pylint: disable=no-name-in-module

from gstgva import VideoFrame

import traceback
import warnings

from deep_sort_realtime.deep_sort.tracker import Tracker
from deep_sort_realtime.deep_sort.detection import Detection
from deep_sort_realtime.deep_sort.nn_matching import NearestNeighborDistanceMetric

Gst.init(None)

VIDEO_CAPS = Gst.Caps.from_string(
    "video/x-raw; video/x-raw(memory:DMABuf); video/x-raw(memory:VASurface)")


OBJECT_CLASS_DEFAULT = ""
REWRITE_ROI_DEFAULT = True
SAVE_LABEL_DEFAULT = True
N_INIT_DEFAULT = 3
MAX_AGE_DEFAULT = 70
MAX_IOU_DISTANCE_DEFAULT = 0.7
NN_BUDGET_DEFAULT = 100


def iou(bbox_1: list, bbox_2: list) -> float:
    xA = max(bbox_1[0], bbox_2[0])
    yA = max(bbox_1[1], bbox_2[1])
    xB = min(bbox_1[0] + bbox_1[2], bbox_2[0] + bbox_2[2])
    yB = min(bbox_1[1] + bbox_1[3], bbox_2[1] + bbox_2[3])

    intersection_area = max(0, xB - xA + 1) * max(0, yB - yA + 1)
    box1_area = bbox_1[2] * bbox_1[3]
    box2_area = bbox_2[2] * bbox_2[3]
    if box1_area + box2_area == intersection_area:
        union_area = intersection_area
    else:
        union_area = box1_area + box2_area - intersection_area

    return intersection_area / union_area


class Identifier(GstBase.BaseTransform):

    __gstmetadata__ = ('ID assignment tracking algorithm', 'Transform',
                       "ID assignment SORT type tracking algorithm which require embedding from each ROI to cosine comparison.",
                       'Intel Corporation')

    __gsttemplates__ = (Gst.PadTemplate.new("sink", Gst.PadDirection.SINK, Gst.PadPresence.ALWAYS, VIDEO_CAPS),
                        Gst.PadTemplate.new("src", Gst.PadDirection.SRC, Gst.PadPresence.ALWAYS, VIDEO_CAPS))

    __gproperties__ = {
        "object-class": (
            str, "object-class",
            "Filter for Region of Interest class label on this element input.",
            OBJECT_CLASS_DEFAULT, GObject.ParamFlags.READWRITE
        ),
        "overwrite-roi": (
            bool, "overwrite-roi",
            "If True, input ROIs will be overwritten with tracker's output.",
            REWRITE_ROI_DEFAULT, GObject.ParamFlags.READWRITE
        ),
        "save-object-class-label": (
            bool, "save-object-class-label",
            """If true, the label from `object-class` will be saved during ROI overwriting.""",
            SAVE_LABEL_DEFAULT, GObject.ParamFlags.READWRITE
        ),
        "n-init": (
            int, "n-init",
            """Number of consecutive detections before the track is confirmed. The
\t\ttrack state is set to `Deleted` if a miss occurs within the first `n-init` frames.""",
            1, GLib.MAXINT, N_INIT_DEFAULT, GObject.ParamFlags.READWRITE
        ),
        "max-age": (
            int, "max-age",
            "Maximum number of missed misses before a track is deleted.",
            0, GLib.MAXINT, MAX_AGE_DEFAULT, GObject.ParamFlags.READWRITE
        ),
        "max-iou-distance": (
            float, "max-iou-distance",
            """The matching IoU threshold. Samples with larger distance are considered
\t\tan invalid match.""",
            0., 1., MAX_IOU_DISTANCE_DEFAULT, GObject.ParamFlags.READWRITE
        ),
        "nn-budget": (
            int, "nn-budget",
            """Fix samples per class to at most this number. Removes
\t\tthe oldest samples when the budget is reached.""",
            0, GLib.MAXINT, NN_BUDGET_DEFAULT, GObject.ParamFlags.READWRITE
        )
    }

    def __init__(self, gproperties=__gproperties__):
        super(Identifier, self).__init__()

        self.property = {}  # default values
        for key, value in gproperties.items():
            self.property[key] = value[3] if value[0] in (
                bool, str) else value[5]

    def __get_properties(self):
        self._object_class = self.property['object-class']
        self._rewrite_roi = self.property['overwrite-roi']
        self._save_label = self.property['save-object-class-label']
        self._nn_budget = self.property['nn-budget']
        self._max_iou_distance = self.property['max-iou-distance']
        self._max_age = self.property['max-age']
        self._n_init = self.property['n-init']

    def __init_on_start(self):
        self.__get_properties()

        metric = NearestNeighborDistanceMetric(
            "cosine", self._max_iou_distance, self._nn_budget
        )
        self._tracker = Tracker(
            metric,
            max_iou_distance=self._max_iou_distance,
            max_age=self._max_age,
            n_init=self._n_init
        )

        self.__write_result = self.__rewrite_regions_with_tracks if self._rewrite_roi else self.__write_ids_to_regions

        if self._save_label and not self._rewrite_roi:
            Gst.warning(
                "`save-object-class-label` property is true while `overwrite-roi` is False.")
        if self._save_label and not self._object_class:
            Gst.warning(
                "`save-object-class-label` property is true while `object-class` is not defined.")
        self._save_label = self._object_class and self._rewrite_roi and self._save_label
        self._label_to_save = self._object_class if self._save_label else ""

        return True

    def do_set_property(self, prop: GObject.GParamSpec, value):
        self.property[prop.name] = value

    def do_get_property(self, prop: GObject.GParamSpec):
        return self.property[prop.name]

    def do_start(self):
        return self.__init_on_start()

    def __get_detections(self, regions):
        detections = []
        for region in regions:
            tensors = [t for t in region.tensors()]
            if len(tensors) > 2:
                # TODO: create special label for embedding
                Gst.warning(
                    f"Limitation: must be only 1 tensor meta per ROI which is embedding, except detection meta.")
                continue

            embedding = None
            for tensor in tensors:
                if not tensor.is_detection():
                    embedding = tensor.data().copy()
                    break
            if embedding is None:
                continue

            bounding_box = list(region.rect())
            confidence = region.confidence()
            detections.append(
                Detection(bounding_box, confidence, embedding))

        return detections

    def __get_tracks(self, detections):
        self._tracker.predict()
        self._tracker.update(detections)

        confirmed_tracks = [
            track for track in self._tracker.tracks
            if track.is_confirmed() and track.time_since_update <= 1
        ]

        return confirmed_tracks

    def __rewrite_regions_with_tracks(self, dst_vf, regions, tracks):
        for region in regions:
            dst_vf.remove_region(region)
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            for track in tracks:
                region = dst_vf.add_region(
                    *track.to_tlwh(), label=self._label_to_save)
                region.set_object_id(int(track.track_id))

    def __write_ids_to_regions(self, dst_vf, regions, tracks):
        for region in regions:
            for track in tracks:
                if iou(list(region.rect()), track.to_tlwh()) > self._max_iou_distance:
                    region.set_object_id(int(track.track_id))
                    break

    def do_transform_ip(self, in_buffer: Gst.Buffer):
        try:
            dst_vf = VideoFrame(in_buffer)

            regions = [r for r in dst_vf.regions()
                       if self._object_class and r.label() == self._object_class
                       or not self._object_class]

            detections = self.__get_detections(regions)

            confirmed_tracks = self.__get_tracks(detections)

            self.__write_result(dst_vf, regions, confirmed_tracks)

            return Gst.FlowReturn.OK
        except Exception as exc:
            Gst.error(f"Error during processing input buffer: {exc}")
            traceback.print_exc()
            return Gst.FlowReturn.ERROR


GObject.type_register(Identifier)

__gstelementfactory__ = ("python_object_association",
                         Gst.Rank.NONE, Identifier)
