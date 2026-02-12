# ==============================================================================
# Copyright (C) 2018-2021 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

## @file audio_frame.py
#  @brief This file contains gstgva.audio_frame.AudioFrame class to control particular inferenced frame
# and attached gstgva.audio_event.AudioEvent and gstgva.tensor.Tensor instances

import ctypes
import numpy
from contextlib import contextmanager
from typing import List
from warnings import warn

import gi
gi.require_version('Gst', '1.0')
gi.require_version("GstAudio", "1.0")
gi.require_version('GObject', '2.0')

# pylint: disable=no-name-in-module
from gi.repository import GObject, Gst, GstAudio
# pylint: enable=no-name-in-module
from .audio_event_meta import AudioEventMeta
from .audio_event import AudioEvent
from ..util import GVATensorMeta
from ..util import GVAJSONMeta
from ..util import GVAJSONMetaStr
from ..tensor import Tensor
from ..util import libgst, gst_buffer_data, AudioInfoFromCaps


## @brief This class represents audio frame - object for working with AudioEvent and Tensor objects which
# belong to this audio frame . AudioEvent describes detected object (segments) and its Tensor
# objects (inference results on AudioEvent level). Tensor describes inference results on AudioFrame level.
# AudioFrame also provides access to underlying GstBuffer and GstAudioInfo describing frame's audio information (such
# as format, channels, etc.).
class AudioFrame:
    ## @brief Construct AudioFrame instance from Gst.Buffer and GstAudio.AudioInfo or Gst.Caps.
    #  The preferred way of creating AudioFrame is to use Gst.Buffer and GstAudio.AudioInfo
    #  @param buffer Gst.Buffer to which metadata is attached and retrieved
    #  @param audio_info GstAudio.AudioInfo containing audio information
    #  @param caps Gst.Caps from which audio information is obtained
    def __init__(self, buffer: Gst.Buffer, audio_info: GstAudio.AudioInfo = None, caps: Gst.Caps = None):
        self.__buffer = buffer
        self.__audio_info = None

        if audio_info:
            self.__audio_info = audio_info
        elif caps:
            self.__audio_info = AudioInfoFromCaps(caps)
        else:
            raise RuntimeError("One of audio_info or caps is required")

    ## @brief Get GstAudio.AudioInfo of this AudioFrame. This is preferrable way of getting audio information
    #  @return GstAudio.AudioInfo of this AudioFrame
    def audio_info(self) -> GstAudio.AudioInfo:
        return self.__audio_info

    ## @brief Get AudioEvent objects attached to AudioFrame
    #  @return iterator of AudioEvent objects attached to AudioFrame
    def events(self):
        return AudioEvent._iterate(self.__buffer)

    ## @brief Get Tensor objects attached to AudioFrame
    #  @return iterator of Tensor objects attached to AudioFrame
    def tensors(self):
        return Tensor._iterate(self.__buffer)

    ## @brief Get messages attached to this AudioFrame
    #  @return GVAJSONMetaStr messages attached to this AudioFrame
    def messages(self) -> List[str]:
        return [json_meta.get_message() for json_meta in GVAJSONMeta.iterate(self.__buffer)]

    ## @brief Attach message to this AudioFrame
    #  @param message message to attach to this AudioFrame
    def add_message(self, message: str):
        GVAJSONMeta.add_json_meta(self.__buffer, message)

    ## @brief Remove message from this AudioFrame
    #  @param message GVAJSONMetaStr message to remove
    def remove_message(self, message: str):
        if not isinstance(message,GVAJSONMetaStr) or not GVAJSONMeta.remove_json_meta(self.__buffer, message.meta):
            raise RuntimeError("AudioFrame: message doesn't belong to this AudioFrame")

    ## @brief Remove audio event with the specified index
    #  @param event audio event to remove
    def remove_event(self, event) -> None:
        if not libgst.gst_buffer_remove_meta(hash(self.__buffer), ctypes.byref(event.meta())):
            raise RuntimeError("AudioFrame: Underlying GstGVAAudioEventMeta for AudioEvent "
                               "doesn't belong to this AudioFrame")

    @staticmethod
    def __get_label_by_label_id(event_tensor: Gst.Structure, label_id: int) -> str:
        if event_tensor and event_tensor.has_field("labels"):
            res = event_tensor.get_array("labels")
            if res[0] and 0 <= label_id < res[1].n_values:
                return res[1].get_nth(label_id)
        return ""

    ## @brief Get buffer data wrapped by numpy.ndarray
    #  @return numpy array representing raw audio samples
    def data(self, flag: Gst.MapFlags = Gst.MapFlags.WRITE) -> numpy.ndarray:
        with gst_buffer_data(self.__buffer, flag) as data:
            try:
                return numpy.ndarray((len(data)), buffer=data, dtype=numpy.uint8)
            except TypeError as e:
                raise e
