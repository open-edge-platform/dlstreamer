# ==============================================================================
# Copyright (C) 2018-2021 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

## @file audio_event.py
#  @brief This file contains gstgva.audio_event.AudioEvent class to control audio events for particular gstgva.audio_frame.AudioFrame with gstgva.tensor.Tensor instances attached

import ctypes
import numpy # pylint: disable=import-error
from typing import List
from collections import namedtuple

from ..tensor import Tensor
from ..util import libgst, libgobject, GLIST_POINTER
from .audio_event_meta import AudioEventMeta

import gi
gi.require_version('GstAudio', '1.0')
gi.require_version('GLib', '2.0')
gi.require_version('Gst', '1.0')
# pylint: disable=no-name-in-module
from gi.repository import GstAudio, GLib, GObject, Gst
# pylint: enable=no-name-in-module

Segment = namedtuple("Segment", "start_time end_time")

## @brief This class represents audio event - object describing detection result (audio segment) and containing multiple
# Tensor objects (inference results) attached by multiple models. For example, it can be audio event with detected
# speech and converts speech to text. It can be produced by a pipeline with gvaaudiodetect with detection model and
# gvaspeechtotext element with speechtotext model. Such AudioEvent will have start and end timestamps filled and will
# have 2 Tensor objects attached - 1 Tensor object with detection result, other with speech to text tensor objectresult

class AudioEvent(object):
    ## @brief Get clip of  AudioEvent as start and end time stamps
    #  @return Start and end time of AudioEvent
    def segment(self):
        return Segment(start_time = self.__event_meta.start_timestamp,
                    end_time = self.__event_meta.end_timestamp)

    ## @brief Get AudioEvent label
    #  @return AudioEvent label
    def label(self) -> str:
        return GLib.quark_to_string(self.__event_meta.event_type)

    ## @brief Get AudioEvent detection confidence (set by gvaaudiodetect)
    # @return last added detection Tensor confidence if exists, otherwise None
    def confidence(self) -> float:
        detection = self.detection()
        return detection.confidence() if detection else None

    ## @brief Get all Tensor instances added to this AudioEvent
    # @return vector of Tensor instances added to this AudioEvent
    def tensors(self):
        param = self.meta()._params
        while param:
            tensor_structure = param.contents.data
            yield Tensor(tensor_structure)
            param = param.contents.next

    ## @brief Returns detection Tensor, last added to this AudioEvent. As any other Tensor, returned detection
    # Tensor can contain arbitrary information. If you use AudioEvent based on GstGVAAudioEventMeta
    # attached by gvaaudiodetect by default, then this Tensor will contain "label_id", "confidence", "start_timestamp",
    # "end_timestamp" fields.
    # If AudioEvent doesn't have detection Tensor, it will be created in-place.
    # @return detection Tensor, empty if there were no detection Tensor objects added to this AudioEvent when
    # this method was called
    def detection(self) -> Tensor:
        for tensor in self.tensors():
            if tensor.is_detection():
                return tensor
        return None

    ## @brief Get label_id from detection Tensor, last added to this AudioEvent
    # @return last added detection Tensor label_id if exists, otherwise None
    def label_id(self) -> int:
        detection = self.detection()
        return detection.label_id() if detection else None

    ## @brief Get AudioEventMeta containing start, end time information and tensors (inference results).
    # Tensors are represented as GstStructures added to GstGVAAudioEventMeta.params
    # @return AudioEventMeta containing start, end time information and tensors (inference results)
    def meta(self) -> AudioEventMeta:
        return self.__event_meta

    ## @brief Iterate by AudioEventMeta instances attached to buffer
    # @param buffer buffer with GstGVAAudioEventMeta instances attached
    # @return generator for AudioEventMeta instances attached to buffer
    @classmethod
    def _iterate(self, buffer: Gst.Buffer):
        try:
            meta_api = hash(GObject.GType.from_name("GstGVAAudioEventMetaAPI"))
        except:
            return
        gpointer = ctypes.c_void_p()
        while True:
            try:
                value = libgst.gst_buffer_iterate_meta_filtered(hash(buffer), ctypes.byref(gpointer), meta_api)
            except:
                value = None

            if not value:
                return

            event_meta = ctypes.cast(value, ctypes.POINTER(AudioEventMeta)).contents
            yield AudioEvent(event_meta)

    ## @brief Construct AudioEvent instance from AudioEventMeta. After this, AudioEvent will
    # obtain all tensors (detection & inference results) from AudioEventMeta
    # @param event_meta AudioEventMeta containing start, end time information and tensors
    def __init__(self, event_meta: AudioEventMeta):
        self.__event_meta = event_meta
