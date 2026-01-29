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
from warnings import warn

gi.require_version("Gst", "1.0")
gi.require_version("GstAnalytics", "1.0")

from enum import Enum
from gi.repository import GObject, Gst, GstAnalytics, GLib
from .util import (
    libgst,
    libgobject,
    G_VALUE_ARRAY_POINTER,
    GValueArray,
    GValue,
    G_VALUE_POINTER,
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
            data_ptr = libgobject.g_variant_get_fixed_array(
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
        self, relation_meta: GstAnalytics.RelationMeta
    ) -> GstAnalytics.Mtd | None:
        mtd = None
        if self.type() == "classification_result":
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
            tensor.set_label(result_label)
            tensor["type"] = "classification_result"
            tensor["confidence"] = result_confidence

            cls_descriptor_mtd = None
            for cls_descriptor_mtd in mtd.meta:
                if (
                    cls_descriptor_mtd.id == mtd.id
                    or type(cls_descriptor_mtd) != GstAnalytics.ClsMtd
                ):
                    continue

                rel = mtd.meta.get_relation(mtd.id, cls_descriptor_mtd.id)

                if rel == GstAnalytics.RelTypes.RELATE_TO:
                    break

                cls_descriptor_mtd = None

            if class_count == 1 and cls_descriptor_mtd is not None:
                label_id = cls_descriptor_mtd.get_index_by_quark(
                    GLib.quark_from_string(result_label)
                )

                if label_id >= 0:
                    tensor["label_id"] = label_id

            return tensor.get_structure()

        return None
