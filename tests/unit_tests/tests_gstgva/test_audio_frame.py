# ==============================================================================
# Copyright (C) 2020-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import sys
import unittest
import ctypes
import gi
import numpy as np

gi.require_version('Gst', '1.0')
gi.require_version('GstAudio', '1.0')

# pylint: disable=no-name-in-module
from gi.repository import Gst, GstAudio
# pylint: enable=no-name-in-module
from gstgva.audio.audio_frame import AudioFrame
from gstgva.audio.audio_event_meta import AudioEventMeta

Gst.init(sys.argv)

from tests_gstgva import register_metadata


class TestAudioFrame(unittest.TestCase):
    def setUp(self):
        register_metadata()
        
        self.buffer = Gst.Buffer.new_allocate(None, 0, None)

        self.meta = AudioEventMeta()
        self.meta._meta_flags = 7
        self.meta.event_type = 9
        self.meta.id = 8
        self.meta.start_timestamp = 1
        self.meta.end_timestamp = 2

        self.audio_info = GstAudio.AudioInfo.new()
        self.audio_info.bpf = 10
        self.audio_info.set_format(GstAudio.AudioFormat.S16LE, 1, 2, None)

        self.audio_frame = AudioFrame(self.buffer, self.audio_info)

    def tearDown(self):
        pass

    def testAlternateConstructor(self):
        caps = self.audio_info.to_caps()
        new_audio_frame = AudioFrame(Gst.Buffer.new_allocate(None, 0, None), None, caps)
        # Check for GST 1.20 API
        if hasattr(GstAudio.AudioInfo, 'new_from_caps'):
            audio_info = GstAudio.AudioInfo.new_from_caps(caps)
        else:
            audio_info = new_audio_frame.audio_info()
        self.assertEqual(audio_info.rate, self.audio_info.rate)
        self.assertEqual(audio_info.channels, self.audio_info.channels)
        self.assertEqual(audio_info.bpf, self.audio_info.bpf)

    def testGetAudioInfo(self):
        audio_info = self.audio_frame.audio_info()
        self.assertEqual(audio_info.rate, self.audio_info.rate)
        self.assertEqual(audio_info.channels, self.audio_info.channels)
        self.assertEqual(audio_info.bpf, self.audio_info.bpf)

    def testMessage(self):
        self.assertEqual(len(self.audio_frame.messages()), 0)
        messages_num = 10
        test_message = "test_messages"
        for i in range(messages_num):
            self.audio_frame.add_message(test_message + str(i))
        messages = self.audio_frame.messages()
        self.assertEqual(len(messages), messages_num)
        for i in range(len(messages)):
            self.audio_frame.remove_message(messages[i])
        self.assertEqual(len(self.audio_frame.messages()), 0)

    def test_tensors(self):
        self.assertEqual(len(list(self.audio_frame.tensors())), 0)

    def test_data(self):

        data=np.array([1, 2, 3])
        buffer= Gst.Buffer.new_wrapped(data)

        frame_from_info = AudioFrame(buffer, self.audio_info)
        self.assertNotEqual(frame_from_info.data().any(), None)
        self.assertEqual(frame_from_info.data().tolist(), data.tolist())

        caps = self.audio_info.to_caps()
        frame_from_buf_caps = AudioFrame(buffer, caps=caps)
        self.assertNotEqual(frame_from_buf_caps.data().any(), None)
        self.assertEqual(frame_from_buf_caps.data().tolist(), data.tolist())
        with self.assertRaises(RuntimeError):
            AudioFrame(buffer)

if __name__ == '__main__':
    unittest.main()
