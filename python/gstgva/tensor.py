# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

## @file tensor.py
#  @brief This file contains gstgva.tensor.Tensor class which contains and describes neural network inference result

import ctypes
import numpy
import gi
from typing import List

gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")
gi.require_version("DLStreamerMeta", "1.0")

from enum import Enum
# pylint: disable=no-name-in-module
from gi.repository import GObject, GstAnalytics, GLib, DLStreamerMeta
# pylint: enable=no-name-in-module
from .util import (
    libgst,
    libgobject,
    libglib,
    G_VALUE_ARRAY_POINTER,
)
from .util import GVATensorMeta


## @brief This class represents tensor - map-like storage for inference result information, such as output blob
# description (output layer dims, layout, rank, precision, etc.), inference result in a raw and interpreted forms.
# Tensor is based on GstStructure and, in general, can contain arbitrary (user-defined) fields of simplest data types,
# like integers, floats & strings.
# Tensor can contain only raw inference result (such Tensor is produced by gvainference in Gstreamer pipeline),
# detection result (such Tensor is produced by gvadetect in Gstreamer pipeline and it's called detection Tensor), or
# both raw & interpreted inference results (such Tensor is produced by gvaclassify in Gstreamer pipeline).
# Tensors can be created and used on their own, or they can be created within RegionOfInterest or VideoFrame instances.
# Usually, in Gstreamer pipeline with GVA elements (gvadetect, gvainference, gvaclassify) Tensor objects will be
# available for access and modification from RegionOfInterest and VideoFrame instances
class Tensor:
    # TODO: find a way to get these enums from C/C++ code and avoid duplicating

    ## @brief This enum describes model layer precision
    class PRECISION(Enum):
        UNSPECIFIED = 255 # Unspecified value. Used by default
        FP32 = 10         # 32bit floating point value
        FP16 = 11         # 16bit floating point value, 5 bit for exponent, 10 bit for mantisa
        BF16 = 12         # 16bit floating point value, 8 bit for exponent, 7 bit for mantis
        FP64 = 13         # 64bit floating point value
        Q78 = 20          # 16bit specific signed fixed point precision
        I16 = 30          # 16bit signed integer value
        U4 = 39           # 4bit unsigned integer value
        U8 = 40           # 8bit unsigned integer value
        I4 = 49           # 4bit signed integer value
        I8 = 50           # 8bit signed integer value
        U16 = 60          # 16bit unsigned integer value
        I32 = 70          # 32bit signed integer value
        U32 = 74          # 32bit unsigned integer value
        I64 = 72          # 64bit signed integer value
        U64 = 73          # 64bit unsigned integer value
        BIN = 71          # 1bit integer value
        BOOL = 41         # 8bit bool type
        CUSTOM = 80        # custom precision has it's own name and size of elements

    __precision_str = {
        PRECISION.UNSPECIFIED: "UNSPECIFIED",
        PRECISION.FP32: "FP32",
        PRECISION.FP16: "FP16",
        PRECISION.BF16: "BF16",
        PRECISION.FP64: "FP64",
        PRECISION.Q78: "Q78",
        PRECISION.I16: "I16",
        PRECISION.U4: "U4",
        PRECISION.U8: "U8",
        PRECISION.I4: "I4",
        PRECISION.I8: "I8",
        PRECISION.U16: "U16",
        PRECISION.I32: "I32",
        PRECISION.U32: "U32",
        PRECISION.I64: "I64",
        PRECISION.U64: "U64",
        PRECISION.BIN: "BIN",
        PRECISION.BOOL: "BOOL",
        PRECISION.CUSTOM: "CUSTOM",
    }

    __precision_numpy_dtype = {
        PRECISION.FP16: numpy.float16,
        PRECISION.FP32: numpy.float32,
        PRECISION.FP64: numpy.float64,
        PRECISION.I8: numpy.int8,
        PRECISION.I16: numpy.int16,
        PRECISION.I32: numpy.int32,
        PRECISION.I64: numpy.int64,
        PRECISION.U8: numpy.uint8,
        PRECISION.U16: numpy.uint16,
        PRECISION.U32: numpy.uint32,
        PRECISION.U64: numpy.uint64,
    }

    ## @brief This enum describes model layer layout
    class LAYOUT(Enum):
        ANY = 0
        NCHW = 1
        NHWC = 2
        NC = 193

    ## @brief Get inference result blob dimensions info
    #  @return list of dimensions
    def dims(self) -> List[int]:
        return self["dims"]

    ## @brief Get inference results blob precision
    #  @return PRECISION, PRECISION.UNSPECIFIED if can't be read
    def precision(self) -> PRECISION:
        precision = self["precision"]

        if precision is None:
            return self.PRECISION.UNSPECIFIED

        return self.PRECISION(precision)

    ## @brief Get inference result blob layout
    #  @return LAYOUT, LAYOUT.ANY if can't be read
    def layout(self) -> LAYOUT:
        try:
            return self.LAYOUT(self["layout"])
        except:
            return self.LAYOUT.ANY

    ## @brief Get raw inference result blob data
    #  @return numpy.ndarray of values representing raw inference data, None if data can't be read
    def data(self) -> numpy.ndarray | None:
        if self.precision() == self.PRECISION.UNSPECIFIED:
            return None

        precision = self.__precision_numpy_dtype[self.precision()]

        gvalue = libgst.gst_structure_get_value(
            self.__structure, "data_buffer".encode("utf-8")
        )

        if gvalue:
            gvariant = libgobject.g_value_get_variant(gvalue)
            nbytes = ctypes.c_size_t()
            data_ptr = libglib.g_variant_get_fixed_array(
                gvariant, ctypes.byref(nbytes), 1
            )
            array_type = ctypes.c_ubyte * nbytes.value
            return numpy.ctypeslib.as_array(array_type.from_address(data_ptr)).view(
                dtype=precision
            )

        return None

    ## @brief Get name as a string
    #  @return Tensor instance's name
    def name(self) -> str:
        name = libgst.gst_structure_get_name(self.__structure)
        if name:
            return name.decode("utf-8")
        return None

    ## @brief Get model name which was used for inference
    #  @return model name as a string, None if failed to get
    def model_name(self) -> str:
        return self["model_name"]

    ## @brief Get inference result blob layer name
    #  @return layer name as a string, None if failed to get
    def layer_name(self) -> str:
        return self["layer_name"]

    ## @brief Get inference result type
    #  @return type as a string, None if failed to get
    def type(self) -> str:
        return self["type"]

    ## @brief Get confidence of inference result
    #  @return confidence of inference result as a float or list of floats, None if failed to get
    def confidence(self) -> float | List[float] | None:
        return self["confidence"]

    ## @brief Get label. This label is set for Tensor instances produced by gvaclassify element. It will raise exception
    # if called for detection Tensor. To get detection class label, use RegionOfInterest.label
    #  @return label as a string, None if failed to get
    def label(self) -> str:
        if not self.is_detection():
            return self["label"]
        else:
            raise RuntimeError("Detection GVA::Tensor can't have label.")

    ## @brief Get object id
    #  @return object id as an int, None if failed to get
    def object_id(self) -> int:
        return self["object_id"]

    ## @brief Get format
    #  @return format as a string, None if failed to get
    def format(self) -> str:
        return self["format"]

    ## @brief Get list of fields contained in Tensor instance
    #  @return List of fields contained in Tensor instance
    def fields(self) -> List[str]:
        return [
            libgst.gst_structure_nth_field_name(self.__structure, i).decode("utf-8")
            for i in range(self.__len__())
        ]

    ## @brief Get item by the field name
    #  @param key Field name
    #  @return Item, None if failed to get
    def __getitem__(self, key):
        key = key.encode("utf-8")
        gtype = libgst.gst_structure_get_field_type(self.__structure, key)
        if gtype == hash(GObject.TYPE_INVALID):  # key is not found
            return None
        elif gtype == hash(GObject.TYPE_STRING):
            res = libgst.gst_structure_get_string(self.__structure, key)
            return res.decode("utf-8") if res else None
        elif gtype == hash(GObject.TYPE_INT):
            value = ctypes.c_int()
            res = libgst.gst_structure_get_int(
                self.__structure, key, ctypes.byref(value)
            )
            return value.value if res else None
        elif gtype == hash(GObject.TYPE_DOUBLE):
            value = ctypes.c_double()
            res = libgst.gst_structure_get_double(
                self.__structure, key, ctypes.byref(value)
            )
            return value.value if res else None
        elif gtype == hash(GObject.TYPE_VARIANT):
            # TODO Returning pointer for now that can be used with other ctypes functions
            #      Return more useful python value
            return libgst.gst_structure_get_value(self.__structure, key)
        elif gtype == hash(GObject.TYPE_POINTER):
            # TODO Returning pointer for now that can be used with other ctypes functions
            #      Return more useful python value
            return libgst.gst_structure_get_value(self.__structure, key)
        else:
            # try to get value as GValueArray (e.g., "dims" key)
            gvalue_array = G_VALUE_ARRAY_POINTER()
            is_array = libgst.gst_structure_get_array(
                self.__structure, key, ctypes.byref(gvalue_array)
            )
            if not is_array:
                # Fallback return value
                libgst.g_value_array_free(gvalue_array)
                return libgst.gst_structure_get_value(self.__structure, key)
            else:
                value = list()
                for i in range(0, gvalue_array.contents.n_values):
                    g_value = libgobject.g_value_array_get_nth(
                        gvalue_array, ctypes.c_uint(i)
                    )
                    if g_value.contents.g_type == hash(GObject.TYPE_FLOAT):
                        value.append(libgobject.g_value_get_float(g_value))
                    elif g_value.contents.g_type == hash(GObject.TYPE_UINT):
                        value.append(libgobject.g_value_get_uint(g_value))
                    elif g_value.contents.g_type == hash(GObject.TYPE_STRING):
                        s = libgobject.g_value_get_string(g_value)
                        value.append(s.decode("utf-8") if s else "")
                    else:
                        raise TypeError("Unsupported value type for GValue array")
                libgst.g_value_array_free(gvalue_array)
                return value

    ## @brief Get number of fields contained in Tensor instance
    #  @return Number of fields contained in Tensor instance
    def __len__(self) -> int:
        return libgst.gst_structure_n_fields(self.__structure)

    ## @brief Iterable by all Tensor fields
    # @return Generator for all Tensor fields
    def __iter__(self):
        for key in self.fields():
            yield key, self.__getitem__(key)

    ## @brief Return string represenation of the Tensor instance
    #  @return String of field names and values
    def __repr__(self) -> str:
        return repr(dict(self))

    ## @brief Remove item by the field name
    #  @param key Field name
    def __delitem__(self, key: str) -> None:
        libgst.gst_structure_remove_field(self.__structure, key.encode("utf-8"))

    ## @brief Get label id
    #  @return label id as an int, None if failed to get
    def label_id(self) -> int:
        return self["label_id"]

    ## @brief Get inference-id property value of GVA element from which this Tensor came
    #  @return inference-id property value of GVA element from which this Tensor came, None if failed to get
    def element_id(self) -> str:
        return self["element_id"]

    ## @brief Set Tensor instance's name
    def set_name(self, name: str) -> None:
        libgst.gst_structure_set_name(self.__structure, name.encode("utf-8"))

    ## @brief Set inference result blob dimensions info
    #  @param dims list of dimensions
    def set_dims(self, dims: List[int]) -> None:
        self.set_vector("dims", dims, value_type="uint")

    ## @brief Set raw data buffer as inference output data
    #  @param data raw bytes or numpy array to store
    def set_data(self, data) -> None:
        if isinstance(data, numpy.ndarray):
            data = data.tobytes()
        vtype = libglib.g_variant_type_new(b'y')
        variant = libglib.g_variant_new_fixed_array(vtype, data, len(data), 1)
        gvalue_variant = GObject.Value()
        gvalue_variant.init(GObject.TYPE_VARIANT)
        libgobject.g_value_set_variant(hash(gvalue_variant), variant)
        libgst.gst_structure_set_value(
            self.__structure, b"data_buffer", hash(gvalue_variant))
        nbytes = ctypes.c_size_t()
        data_ptr = libglib.g_variant_get_fixed_array(variant, ctypes.byref(nbytes), 1)
        gvalue_pointer = GObject.Value()
        gvalue_pointer.init(GObject.TYPE_POINTER)
        libgobject.g_value_set_pointer(hash(gvalue_pointer), data_ptr)
        libgst.gst_structure_set_value(
            self.__structure, b"data", hash(gvalue_pointer))

    ## @brief Set inference output blob precision
    #  @param precision PRECISION enum value
    def set_precision(self, precision: 'Tensor.PRECISION') -> None:
        self["precision"] = int(precision.value)

    ## @brief Set tensor type as a string
    #  @param type_str type of tensor
    def set_type(self, type_str: str) -> None:
        self["type"] = type_str

    ## @brief Set data format
    #  @param format_str format string
    def set_format(self, format_str: str) -> None:
        self["format"] = format_str

    ## @brief Set confidence of detection or classification result
    #  @param confidence confidence value
    def set_confidence(self, confidence: float) -> None:
        self["confidence"] = float(confidence)

    ## @brief Set a GValueArray field on the underlying GstStructure
    #  @param field_name name of the field
    #  @param data list of values to store
    #  @param value_type type hint: "float", "uint", or "string". If None, inferred from first element.
    def set_vector(self, field_name: str, data: list, value_type: str = None) -> None:
        if not data:
            return
        if value_type is None:
            first = data[0]
            if isinstance(first, float):
                value_type = "float"
            elif isinstance(first, int):
                value_type = "uint"
            elif isinstance(first, str):
                value_type = "string"
            else:
                raise TypeError(f"Unsupported element type: {type(first)}")

        gvalue_array = libgobject.g_value_array_new(len(data))
        for item in data:
            gval = GObject.Value()
            if value_type == "float":
                gval.init(GObject.TYPE_FLOAT)
                gval.set_float(float(item))
            elif value_type == "uint":
                gval.init(GObject.TYPE_UINT)
                gval.set_uint(int(item))
            elif value_type == "string":
                gval.init(GObject.TYPE_STRING)
                gval.set_string(str(item))
            else:
                raise TypeError(f"Unsupported value_type: {value_type}")
            libgobject.g_value_array_append(gvalue_array, hash(gval))
        libgst.gst_structure_set_array(
            self.__structure, field_name.encode("utf-8"), gvalue_array)

    ## @brief Get inference result blob layout as a string
    #  @return layout as a string, "ANY" if can't be read
    def layout_as_string(self) -> str:
        layout = self.layout()
        if layout == self.LAYOUT.NCHW:
            return "NCHW"
        elif layout == self.LAYOUT.NHWC:
            return "NHWC"
        elif layout == self.LAYOUT.NC:
            return "NC"
        else:
            return "ANY"

    ## @brief Get inference results blob precision as a string
    #  @return precision as a string, "UNSPECIFIED" if can't be read
    def precision_as_string(self) -> str:
        return self.__precision_str[self.precision()]

    ## @brief Set label. It will raise exception if called for detection Tensor
    #  @param label label name as a string
    def set_label(self, label: str) -> None:
        if not self.is_detection():
            self["label"] = label
        else:
            raise RuntimeError("Detection GVA::Tensor can't have label.")

    ## @brief Check if Tensor instance has field
    #  @param field_name field name
    #  @return True if field with this name is found, False otherwise
    def has_field(self, field_name: str) -> bool:
        return True if self[field_name] else False

    ## @brief Check if this Tensor is detection Tensor (contains detection results)
    #  @return True if tensor contains detection results, False otherwise
    def is_detection(self) -> bool:
        return self.name() == "detection"

    ## @brief Get underlying GstStructure
    #  @return C-style pointer to GstStructure
    def get_structure(self) -> ctypes.c_void_p:
        return self.__structure

    ## @brief Construct Tensor instance from C-style GstStructure
    #  @param structure C-style pointer to GstStructure to create Tensor instance from.
    # There are much simpler ways for creating and obtaining Tensor instances - see RegionOfInterest and VideoFrame classes
    def __init__(self, structure: ctypes.c_void_p):
        self.__structure = structure
        if not self.__structure:
            raise ValueError("Tensor: structure passed is nullptr")

    ## @brief Set item to Tensor. It can be one of the following types: string, int, float.
    #  @param key Name of new field
    #  @param item Item
    def __setitem__(self, key: str, item) -> None:
        gvalue = GObject.Value()
        if type(item) is str:
            gvalue.init(GObject.TYPE_STRING)
            gvalue.set_string(item)
        elif type(item) is int:
            gvalue.init(GObject.TYPE_INT)
            gvalue.set_int(item)
        elif type(item) is float:
            gvalue.init(GObject.TYPE_DOUBLE)
            gvalue.set_double(item)
        elif type(item) is list:
            # code below doesn't work though it's very similar to C code used in GVA which works
            # gvalue_array = GObject.Value()
            # libgobject.g_value_init(hash(gvalue), ctypes.c_size_t(24))  # 24 is G_TYPE_INT
            # libgobject.g_value_init(hash(gvalue_array), libgst.gst_value_array_get_type())
            # for i in item:
            #     libgobject.g_value_set_int(hash(gvalue),i)
            #     libgst.gst_value_array_append_value(hash(gvalue_array),hash(gvalue))
            # libgst.gst_structure_set_value(self.__structure, key.encode('utf-8'), hash(gvalue_array))
            raise NotImplementedError
        else:
            raise TypeError
        libgst.gst_structure_set_value(
            self.__structure, key.encode("utf-8"), hash(gvalue)
        )

    @classmethod
    def _iterate(cls, buffer):
        try:
            meta_api = hash(GObject.GType.from_name("GstGVATensorMetaAPI"))
        except:
            return

        gpointer = ctypes.c_void_p()
        while True:
            try:
                value = libgst.gst_buffer_iterate_meta_filtered(
                    hash(buffer), ctypes.byref(gpointer), meta_api
                )
            except Exception as error:
                value = None

            if not value:
                return

            tensor_meta = ctypes.cast(value, ctypes.POINTER(GVATensorMeta)).contents
            yield Tensor(tensor_meta.data)

    def convert_to_meta(
        self, relation_meta: GstAnalytics.RelationMeta, od_meta: GstAnalytics.ODMtd = None
    ) -> GstAnalytics.Mtd | None:
        ## @brief Convert this tensor to GstAnalytics metadata
        #  @param relation_meta GstAnalyticsRelationMeta to attach the metadata to
        #  @param od_meta parent object-detection metadata (required for keypoints)
        #  @return GstAnalyticsMtd on success, None if tensor type is not supported
        mtd = None
        if self.type() == "keypoints":
            dimensions = self.dims()
            raw_data = self.data()
            confidence_val = self.confidence()
            keypoint_count = list(dimensions)[0]
            keypoint_dimension = list(dimensions)[1]

            dim = DLStreamerMeta.KeypointDimensions(keypoint_dimension)

            # get screen space coordinates of the parent bounding box
            if od_meta is None:
                raise ValueError("od_meta is required for keypoints conversion")
            success, x, y, w, h, conf = od_meta.get_location()
            if not success:
                raise RuntimeError("Failed to read object detection meta")

            # convert float positions to integer pixel coordinates
            stride = 3 if dim == DLStreamerMeta.KeypointDimensions(3) else 2
            positions = []
            for k in range(keypoint_count):
                px = x + int(w * raw_data[k * keypoint_dimension])
                py = y + int(h * raw_data[k * keypoint_dimension + 1])
                positions.append(px)
                positions.append(py)
                if stride == 3:
                    pz = int(raw_data[k * keypoint_dimension + 2])
                    positions.append(pz)

            # confidences
            if isinstance(confidence_val, list):
                confidences = confidence_val
            elif confidence_val is not None:
                confidences = [confidence_val]
            else:
                confidences = [0.0] * keypoint_count

            # look up skeleton connections from descriptor
            semantic_tag = self.format() or ""
            descriptor = DLStreamerMeta.KeypointDescriptor.lookup(semantic_tag)
            skeleton = None
            if descriptor and descriptor.get_skeleton_connection_count() > 0:
                skeleton = []
                for i in range(descriptor.get_skeleton_connection_count()):
                    ok, from_idx, to_idx = descriptor.get_skeleton_connection(i)
                    if ok:
                        skeleton.append(from_idx)
                        skeleton.append(to_idx)

            ok, group_mtd = DLStreamerMeta.relation_meta_add_keypoints_group(
                relation_meta, semantic_tag, dim,
                positions, confidences, None, skeleton)
            if not ok:
                raise RuntimeError("Failed to create keypoints group meta")

            mtd = group_mtd

        elif self.type() == "classification_result":
            confidence_level = (
                self.confidence() if self.confidence() is not None else 0.0
            )

            class_quark = (
                GLib.quark_from_string(self.label()) if self.label() is not None else 0
            )

            success, mtd = relation_meta.add_one_cls_mtd(confidence_level, class_quark)

            if not success:
                raise RuntimeError(
                    "Failed to add classification metadata to RelationMeta"
                )

        return mtd

    @staticmethod
    def convert_to_tensor(mtd: GstAnalytics.Mtd) -> ctypes.c_void_p | None:
        ## @brief Convert GstAnalytics metadata back to a GstStructure tensor
        #  @param mtd GstAnalyticsMtd (GroupMtd for keypoints, ClsMtd for classification)
        #  @return pointer to newly created GstStructure, or None if metadata type is not supported
        if type(mtd) == DLStreamerMeta.GroupMtd:
            group_mtd = mtd
            keypoint_count = group_mtd.get_member_count()
            if keypoint_count == 0:
                return None

            keypoint_dimension = 2

            # find parent bounding box
            x, y, w, h = 0, 0, 0, 0
            for rlt_mtd in group_mtd.meta:
                if rlt_mtd.id == group_mtd.id or type(rlt_mtd) != GstAnalytics.ODMtd:
                    continue
                rel = group_mtd.meta.get_relation(group_mtd.id, rlt_mtd.id)
                if rel == GstAnalytics.RelTypes.IS_PART_OF:
                    success, x, y, w, h, _ = rlt_mtd.get_location()
                    if not success:
                        raise RuntimeError("Failed to read object detection meta")
                    break

            # read positions and confidences from group members
            kp_data = []
            kp_type = DLStreamerMeta.KeypointMtd.get_mtd_type()
            state = None
            while True:
                ok, state, member = group_mtd.iterate(state, kp_type)
                if not ok:
                    break
                ok, kp = DLStreamerMeta.relation_meta_get_keypoint_mtd(
                    group_mtd.meta, member.id)
                if not ok:
                    continue
                ok, px, py, pz, dim = kp.get_position()
                if not ok:
                    continue
                ok, conf = kp.get_confidence()
                if not ok:
                    conf = 0.0
                if pz != 0:
                    keypoint_dimension = 3
                kp_data.append((px, py, pz, conf))

            keypoint_count = len(kp_data)
            if keypoint_count == 0:
                return None

            # convert to float position/confidence arrays
            positions = []
            confidences = []
            for px, py, pz, conf in kp_data:
                positions.append((px - x) / w if w > 0 else 0.0)
                positions.append((py - y) / h if h > 0 else 0.0)
                if keypoint_dimension == 3:
                    positions.append(float(pz))
                confidences.append(conf)

            # read point names from descriptor
            semantic_tag = group_mtd.get_semantic_tag() or ""
            descriptor = DLStreamerMeta.KeypointDescriptor.lookup(semantic_tag) if semantic_tag else None
            point_names = []
            if descriptor and descriptor.get_point_count() == keypoint_count:
                point_names = [descriptor.get_point_name(k) for k in range(keypoint_count)]

            # reconstruct skeleton from RELATE_TO relations between keypoints
            kp_ids = []
            for k in range(keypoint_count):
                ok, m = group_mtd.get_member(k)
                if ok:
                    kp_ids.append(m.id)
                else:
                    kp_ids.append(None)

            point_connections = []
            for i in range(keypoint_count):
                for j in range(i + 1, keypoint_count):
                    if kp_ids[i] is not None and kp_ids[j] is not None:
                        rel_ij = group_mtd.meta.get_relation(kp_ids[i], kp_ids[j])
                        if rel_ij == GstAnalytics.RelTypes.RELATE_TO:
                            point_connections.append(i)
                            point_connections.append(j)
                            continue
                        rel_ji = group_mtd.meta.get_relation(kp_ids[j], kp_ids[i])
                        if rel_ji == GstAnalytics.RelTypes.RELATE_TO:
                            point_connections.append(j)
                            point_connections.append(i)

            # create keypoint tensor
            structure = libgst.gst_structure_new_empty("keypoints".encode("utf-8"))
            tensor = Tensor(structure)

            tensor.set_precision(Tensor.PRECISION.FP32)
            tensor.set_type("keypoints")
            tensor.set_format(semantic_tag)

            tensor.set_dims([keypoint_count, keypoint_dimension])
            tensor.set_data(numpy.array(positions, dtype=numpy.float32))

            tensor.set_vector("confidence", confidences, value_type="float")
            if point_names:
                tensor.set_vector("point_names", point_names, value_type="string")
            if point_connections:
                tensor.set_vector("point_connections", point_connections, value_type="uint")

            return tensor.get_structure()

        structure = libgst.gst_structure_new_empty("tensor".encode("utf-8"))
        tensor = Tensor(structure)

        if type(mtd) == GstAnalytics.ClsMtd:
            class_count = mtd.get_length()
            result_confidence = 0.0
            result_label = ""

            for i in range(class_count):
                confidence = mtd.get_level(i)
                if confidence < 0.0:
                    raise RuntimeError("Negative confidence level in metadata")

                quark_label = mtd.get_quark(i)
                label = GLib.quark_to_string(quark_label) if quark_label else ""

                if label:
                    if result_label and not result_label[-1].isspace():
                        result_label += " "
                    result_label += label

                if confidence > result_confidence:
                    result_confidence = confidence

            tensor.set_name("classification")
            tensor["type"] = "classification_result"
            tensor.set_label(result_label)
            tensor["confidence"] = result_confidence

            cls_descriptor_mtd = None
            for rlt_mtd in mtd.meta:
                if (
                    rlt_mtd.id == mtd.id
                    or type(rlt_mtd) != GstAnalytics.ClsMtd
                ):
                    continue

                rel = mtd.meta.get_relation(mtd.id, rlt_mtd.id)

                if rel == GstAnalytics.RelTypes.RELATE_TO:
                    cls_descriptor_mtd = rlt_mtd
                    break

            if class_count == 1 and cls_descriptor_mtd is not None:
                label_id = cls_descriptor_mtd.get_index_by_quark(
                    GLib.quark_from_string(result_label)
                )

                if label_id >= 0:
                    tensor["label_id"] = label_id

            return tensor.get_structure()

        return None
