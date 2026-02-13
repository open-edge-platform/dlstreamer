# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import unittest

import gi
gi.require_version('Gst', '1.0')
gi.require_version("GstVideo", "1.0")
gi.require_version("GLib", "2.0")
from gi.repository import GstVideo, GLib, Gst, GObject # pylint: disable=no-name-in-module

from gstgva.util import libgst
from gstgva.tensor import Tensor

class TensorTestCase(unittest.TestCase):
    def setUp(self):
        pass

    def test_tensor(self):
        test_obj_id = 1
        test_label_id = 2
        test_confidence = 0.5

        structure = libgst.gst_structure_new_empty('classification'.encode("utf-8"))
        tensor = Tensor(structure)

        self.assertEqual(tensor.name(), "classification")
        self.assertFalse(tensor.is_detection())

        tensor["layer_name"] = "test_layer_name"
        self.assertEqual(tensor.has_field("layer_name"), True)
        self.assertEqual(tensor["layer_name"], tensor.layer_name())
        self.assertEqual(tensor.fields(), ["layer_name"])

        tensor["model_name"] = "test_model_name"
        self.assertEqual(tensor.has_field("model_name"), True)
        self.assertEqual(tensor["model_name"], tensor.model_name())

        expected_fields = ["layer_name", "model_name"]
        for field, _ in tensor:
            if field in expected_fields:
                expected_fields.remove(field)
        self.assertEqual(expected_fields, [])

        tensor["element_id"] = "test_element_id"
        self.assertEqual(tensor.has_field("element_id"), True)
        self.assertEqual(tensor["element_id"], tensor.element_id())
        self.assertEqual(len(tensor.fields()), 3)

        tensor["format"] = "test_format"
        self.assertEqual(tensor.has_field("format"), True)
        self.assertEqual(tensor["format"], tensor.format())

        tensor["label"] = "test_label"
        self.assertEqual(tensor.has_field("label"), True)
        self.assertEqual(tensor["label"], tensor.label())

        tensor["label_id"] = test_label_id
        self.assertEqual(tensor.has_field("label_id"), True)
        self.assertEqual(tensor["label_id"], tensor.label_id())

        tensor["object_id"] = test_obj_id
        self.assertEqual(tensor.has_field("object_id"), True)
        self.assertEqual(tensor["object_id"], tensor.object_id())

        tensor["confidence"] = test_confidence
        self.assertEqual(tensor.has_field("confidence"), True)
        self.assertEqual(tensor["confidence"], tensor.confidence())

        tensor["precision"] = Tensor.PRECISION.U8.value
        self.assertEqual(tensor.has_field("precision"), True)
        self.assertEqual((Tensor.PRECISION)(
            tensor.__getitem__("precision")), tensor.precision())

        tensor["layout"] = Tensor.LAYOUT.NCHW.value
        self.assertEqual(tensor.has_field("layout"), True)
        self.assertEqual((Tensor.LAYOUT)(tensor["layout"]), tensor.layout())

        tensor["rank"] = 1
        self.assertEqual(tensor.has_field("rank"), True)
        self.assertEqual(len(tensor.fields()), 11)

        self.assertEqual(tensor.layout_as_string(), "NCHW")
        self.assertEqual(tensor.precision_as_string(), "U8")

        # Currently Tensor.__setitem__ for list -> GValueArray of GstStructure is not implemented (technical issues)
        # dims = [1, 2, 3]
        # tensor["dims"] = dims
        # idx=0
        # dims = tensor.dims()
        # print(dims)
        # for i in dims:
        #     self.assertEqual(i, libgobject.g_value_get_int(libgobject.g_value_array_get_nth(test_array, ctypes.c_uint(idx)))
        #     idx += 1

    def tearDown(self):
        pass


if __name__ == '__main__':
    unittest.main()
