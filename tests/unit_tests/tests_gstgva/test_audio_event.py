# ==============================================================================
# Copyright (C) 2020-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import ctypes
import unittest
from gstgva.util import GList, libgst, GLIST_POINTER
from gstgva.tensor import Tensor
from gstgva.audio.audio_event import AudioEvent
from gstgva.audio.audio_event_meta import AudioEventMeta
import gi
gi.require_version('GLib', '2.0')
from gi.repository import GLib # pylint: disable=no-name-in-module, wrong-import-position


class TestAudioEvent(unittest.TestCase):
    tensors_count = 2
    confidence_value = 1
    label_id = 9
    def setUp(self):

        classification_structure = libgst.gst_structure_new_empty('classification'.encode("utf-8"))
        detection_structure = libgst.gst_structure_new_empty('detection'.encode("utf-8"))
        tensor = Tensor(detection_structure)
        tensor.__setitem__("confidence", self.confidence_value)
        tensor.__setitem__("label_id", self.label_id)

        lst = GList()
        lst2 = GList()
        lst.data = ctypes.c_void_p(classification_structure)
        lst.next = GLIST_POINTER(lst2)
        lst.prev = None
        lst2.data = ctypes.c_void_p(detection_structure)
        lst2.next = None
        lst2.prev = GLIST_POINTER(lst)

        self.meta = AudioEventMeta()
        self.meta._meta_flags = 7
        self.meta.event_type = 9
        self.meta.id = 8
        self.meta.start_timestamp = 1
        self.meta.end_timestamp = 2
        self.meta._params = GLIST_POINTER(lst)
        self.audio_event = AudioEvent(self.meta)

    def test_audio_event_segment(self):
        seg = self.audio_event.segment()
        self.assertEqual(seg.start_time, self.meta.start_timestamp)
        self.assertEqual(seg.end_time, self.meta.end_timestamp)

    def test_audio_event_segment_negative(self):
        meta = AudioEventMeta()
        audio_event = AudioEvent(meta)
        seg = audio_event.segment()
        self.assertEqual(seg.start_time, 0)
        self.assertEqual(seg.end_time, 0)

    def test_audio_event_label(self):
        label = self.audio_event.label()
        self.assertEqual(label, GLib.quark_to_string(self.meta.event_type))

    def test_tensors(self):
        tensors_count = 0
        for tensor in self.audio_event.tensors():
            tensors_count+=1
        self.assertEqual(tensors_count, self.tensors_count)

    def test_audio_event_label_id(self):
        self.assertEqual(self.audio_event.label_id(), self.label_id)

    def test_audio_event_label_id_negative(self):
        meta = AudioEventMeta()
        audio_event = AudioEvent(meta)
        self.assertEqual(audio_event.label_id(), None)

    def test_audio_event_confidence(self):
        self.assertEqual(self.audio_event.confidence(), self.confidence_value)

    def test_audio_event_confidence_None(self):
        meta = AudioEventMeta()
        audio_event = AudioEvent(meta)
        self.assertEqual(audio_event.confidence(), None)

    def test_audio_event_iterate_negative(self):
        ret = [n for n in AudioEvent._iterate(None)]
        self.assertEqual(ret, [])

    def test_audio_event_meta(self):
        self.assertEqual(self.audio_event.meta(), self.meta)

    def tearDown(self):
        pass

if __name__ == '__main__':
    unittest.main()
