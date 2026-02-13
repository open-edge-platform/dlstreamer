# ==============================================================================
# Copyright (C) 2021-2025 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst # pylint: disable=no-name-in-module

class MyClass:
    def __init__(self):
        print("MyClass.__init__")

    def process_frame(self, frame):
        print("MyClass.process_frame")
        return True

    def my_function(self, frame):
        print("MyClass.my_function")
        return True


class MyClassWithArgs:
    def __init__(self, path, **kwargs):
        print("MyClassWithArgs.__init__")

    def process_frame(self):
        print("MyClassWithArgs.process_frame")


class MyClassVaapi:
    def __init__(self):
        print("MyClassVaapi.__init__")

    def read_frame_data(self, frame):
        print("MyClassVaapi.read_frame_data")
        with frame.data() as mat:
            print(
                f"MyClassVaapi.read_frame_data: Get frame.data() size {mat.nbytes}")

    def write_frame_data(self, frame):
        print("MyClassVaapi.write_frame_data")
        with frame.data(Gst.MapFlags.WRITE) as mat:
            print(
                f"MyClassVaapi.write_frame_data: Get frame.data() size {mat.nbytes}")


def process_frame(frame):
    print("Function: process_frame")
    return True
