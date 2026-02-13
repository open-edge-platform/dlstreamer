#!/usr/bin/python3

# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstBase', '1.0')
gi.require_version('GstVideo', '1.0')

from gi.repository import Gst, GObject, GstBase # pylint: disable=no-name-in-module

import torch
import numpy as np
import traceback
import importlib

from gstgva import VideoFrame
from typing import List
from torchvision.transforms._presets import ImageClassification


Gst.init(None)


TENSORS_CAPS = Gst.Caps.from_string("other/tensors")

TORCHVISION_PREFIX = "torchvision.models"
PYTROCH_MODELS_EXT = (".pth", ".pt")


class TensorInfo:
    def __init__(self, shape: list, data_type, strides: list) -> None:
        self.shape = tuple(shape)
        self.strides = tuple(strides)
        self.data_type = data_type


def str_to_datatype(data_type: str):
    if data_type == "float32":
        return np.float32
    if data_type == "uint8":
        return np.uint8
    if data_type == "int64":
        return np.int64
    if data_type == "int32":
        return np.int32

    raise RuntimeError(
        "Unknown data type. Can't convert it to numpy data type")


def datatype_to_str(data_type) -> str:
    if data_type == np.float32 or data_type == torch.float32:
        return "float32"
    if data_type == np.uint8 or data_type == torch.uint8:
        return "uint8"
    if data_type == np.int64 or data_type == torch.int64:
        return "int64"
    if data_type == np.int32 or data_type == torch.int32:
        return "int32"

    raise RuntimeError("Unknown data type. Can't convert it to str")


def str_to_list(arr: str) -> list:
    if not arr:
        return list()

    res = map(lambda x: int(x), arr.split(':'))
    return list(res)


def tuple_to_str(arr: tuple) -> str:
    if not arr:
        return ""

    return ":".join(str(d) for d in arr)


def gst_caps_to_tensor_info(caps: Gst.Caps, caps_index: int) -> List[TensorInfo]:
    tensors_info = list()

    gst_struct = caps.get_structure(caps_index)
    (status, num_tensors) = gst_struct.get_uint("num_tensors")
    if not status or not num_tensors:
        return tensors_info

    types_str = gst_struct.get_string("types")
    if not types_str:
        raise RuntimeError("Tensor type not specified in caps structure")
    shapes_str = gst_struct.get_string("dimensions")
    if not shapes_str:
        shapes_str = ""
    strides_str = gst_struct.get_string("strides")
    if not strides_str:
        strides_str = ""

    types_array = types_str.split(',')
    shapes_array = shapes_str.split(',') if shapes_str else list()
    strides_array = strides_str.split(',') if strides_str else list()

    for i in range(num_tensors):
        data_type = str_to_datatype(types_array[i])
        shape = str_to_list(shapes_array[i]) if shapes_array else list()
        stride = str_to_list(strides_array[i]) if strides_array else list()

        # reverse order
        shape.reverse()
        stride.reverse()

        tensors_info.append(TensorInfo(shape, data_type, stride))

    return tensors_info


def tensor_info_to_gst_caps(tensors_info: List[TensorInfo]) -> Gst.Caps:
    if not tensors_info:
        return Gst.Caps.new_any()

    dtypes_str = ",".join(datatype_to_str(tensor_info.data_type)
                          for tensor_info in tensors_info)
    shapes_str = ",".join(":".join(
        str(d) for d in tensor_info.shape[::-1]) for tensor_info in tensors_info)
    caps_str = f"other/tensors,num_tensors=(uint){len(tensors_info)},types=(string)\"{dtypes_str}\",dimensions=(string)\"{shapes_str}\""

    return Gst.Caps.from_string(caps_str)


def is_torchvision_module(model_str: str) -> bool:
    return model_str.startswith(TORCHVISION_PREFIX)


def is_pytorch_model(model_str: str) -> bool:
    return model_str.endswith(PYTROCH_MODELS_EXT)


class InferencePyTorch(GstBase.BaseTransform):
    __gstmetadata__ = ('PyTorch inference', 'Transform',
                       'PyTorch inference element', 'dkl')

    __gsttemplates__ = (Gst.PadTemplate.new("sink", Gst.PadDirection.SINK, Gst.PadPresence.ALWAYS, TENSORS_CAPS),
                        Gst.PadTemplate.new("src", Gst.PadDirection.SRC, Gst.PadPresence.ALWAYS, TENSORS_CAPS))

    # TODO: support batching
    __gproperties__ = {
        "model": (GObject.TYPE_STRING, "model", "The full module name of the PyTorch model to be imported from torchvision or model path. Ex. 'torchvision.models.resnet50' or '/path/to/model.pth'", "", GObject.ParamFlags.READWRITE),
        "model-weights": (GObject.TYPE_STRING, "model_weights", "PyTorch model weights path. If model-weights is empty, the default weights will be used", "", GObject.ParamFlags.READWRITE),
        "device": (GObject.TYPE_STRING, "device", "Inference device", "cpu", GObject.ParamFlags.READWRITE)
    }

    def __init__(self, gproperties=__gproperties__):
        super(InferencePyTorch, self).__init__()

        self.property = {}  # default values
        for key, value in gproperties.items():
            self.property[key] = value[3] if value[0] in (
                bool, str, GObject.TYPE_STRING, GObject.TYPE_BOOLEAN) else value[5]

        self.device = torch.device(self.property["device"])
        self.model = None
        self.weights = None
        self.input_tensors_info = list()
        self.output_tensors_info = list()
        self.gst_alloc = Gst.Allocator.find()

    def init_pytorch_model(self):
        model_str = self.property["model"]
        if not model_str:
            raise AttributeError(f"'model' property is empty")

        if is_pytorch_model(model_str):
            self.model = torch.load(model_str, map_location=self.device)
        elif is_torchvision_module(model_str):
            model_name_arr = model_str.split(".")
            if len(model_name_arr) < 2:
                raise AttributeError(
                    f"Invalid module name in property 'model'. It must include at least the name of the import module and the name of the model. Got: {model_str}")

            model_name = model_name_arr[-1]
            import_module = ".".join(module for module in model_name_arr[:-1])
            module = importlib.import_module(import_module)
            creator = getattr(module, model_name)

            if self.property["model-weights"]:
                self.model = creator()
                self.model.load_state_dict(
                    torch.load(self.property["model-weights"]))
            else:
                weights_class = None
                for attr in dir(module):
                    if attr.lower() == model_name + "_weights":
                        weights_class = getattr(module, attr)
                        break

                model_args = dict()
                if weights_class:
                    self.weights = weights_class.DEFAULT
                    model_args["weights"] = weights_class.DEFAULT
                else:
                    Gst.warning(
                        "No suitable class with weights was found in the module. No pre-trained weights are used")

                self.model = creator(**model_args)
        else:
            raise AttributeError(f"Invalid or unknown model: {model_str}")

        self.model.eval()

    def do_start(self):
        if not self.model:
            Gst.error("Model is None. Something went wrong during initialization")
            return False

        return True

    def do_get_property(self, prop: GObject.GParamSpec):
        return self.property[prop.name]

    def do_set_property(self, prop: GObject.GParamSpec, value):
        if prop.name == "device":
            self.device = torch.device(value)

        self.property[prop.name] = value

    def do_transform_caps(self, direction, caps, filter):
        try:
            if not self.model:
                self.init_pytorch_model()

            if direction == Gst.PadDirection.SINK:
                if not self.output_tensors_info:
                    for i in range(caps.get_size()):
                        input_tensors_info = gst_caps_to_tensor_info(caps, i)
                        output_tensors_info = self.get_output_tensors_info(
                            input_tensors_info)
                        if output_tensors_info:
                            self.input_tensors_info = input_tensors_info
                            self.output_tensors_info = output_tensors_info
                            break

                my_caps = tensor_info_to_gst_caps(self.output_tensors_info)
            else:
                if not self.input_tensors_info:
                    self.input_tensors_info = self.get_input_tensor_info_from_model()
                # Input shapes must be specified with capsfilter before the tensor_convert element if preprocessing information could not be obtained from the model
                my_caps = tensor_info_to_gst_caps(self.input_tensors_info)

            if filter:
                my_caps = my_caps.intersect(filter)
            return my_caps
        except Exception as exc:
            Gst.error(f"Error while transform caps: {exc}")
            traceback.print_exc()
            return Gst.Caps.new_empty()

    def get_output_tensors_info(self, input_tensors_info: List[TensorInfo]) -> List[TensorInfo]:
        if len(input_tensors_info) != 1:  # TODO: remove limitation. Support models with multiple inputs
            raise RuntimeError(
                f"Models with one layer only are supported now. Got tensors array from caps size of: {len(input_tensors_info)}")

        output_tensors_info = list()
        input_tensor_info = input_tensors_info[0]
        if not input_tensor_info.shape:
            return output_tensors_info  # wait shapes from upstream elements

        if not self.model:
            raise AttributeError("Model is None")

        self.model.to(self.device)  # load model to device

        try:
            # The forward function can contain arbitrary python code. Forward dummy tensor to see the output
            x = torch.randn(input_tensor_info.shape, device=self.device)
            x = x.unsqueeze(0)  # add batch dimension
            output = self.model(x)[0]  # TODO: support batching
        except Exception as model_exc:
            Gst.debug(
                f"Model inferencing failed at caps transform stage: {str(model_exc)}")
            return output_tensors_info  # wait for the next input tensor shape

        if isinstance(output, torch.Tensor):
            output_tensors_info.append(
                TensorInfo(output.shape, output.dtype, []))
        elif isinstance(output, dict):
            for tensor in output.values():
                output_tensors_info.append(
                    TensorInfo(tensor.shape, tensor.dtype, []))
        else:
            raise RuntimeError("Unsupported model output")

        return output_tensors_info

    def get_input_tensor_info_from_model(self) -> List[TensorInfo]:
        input_tensors_info = list()
        if not self.weights:
            return input_tensors_info

        preproc = self.weights.transforms()

        # TODO: get useful information from other types of preprocessing?
        if not isinstance(preproc, ImageClassification):
            return input_tensors_info

        # TODO: Parse the remaining preprocessing information and pass it to the preprocessing sub-pipeline
        resize_size = preproc.resize_size
        if len(resize_size) != 1:
            Gst.warning(
                "Unsupported resize size. Unable to parse preprocessing info from weights. Default values from caps will be used")
        else:
            input_tensors_info.append(TensorInfo(
                [3, resize_size[0], resize_size[0]], torch.float32, []))

        return input_tensors_info

    def do_set_caps(self, incaps, outcaps):
        try:
            self.input_tensors_info = gst_caps_to_tensor_info(incaps, 0)
            self.output_tensors_info = gst_caps_to_tensor_info(outcaps, 0)

            if not self.gst_alloc:
                raise RuntimeError("Allocator not found")

            return True
        except Exception as exc:
            Gst.error(f"Failed to set caps: {exc}")
            traceback.print_exc()
            return False

    def add_model_info(self, buf):
        dst_vf = VideoFrame(buf)
        gva_tensor = dst_vf.add_tensor()
        if gva_tensor is None:
            raise RuntimeError("Unable to add 'GstGVATensorMeta' metadata")

        input_shapes = ""
        input_types = ""
        for i, tensor_info in enumerate(self.input_tensors_info):
            if (i):
                input_shapes += ","
                input_types += ","

            input_shapes += tuple_to_str(tensor_info.shape)
            input_types += datatype_to_str(tensor_info.data_type)

        # TODO: complete gva_tensor if possible
        gva_tensor.set_name("model_info")
        gva_tensor["model_name"] = self.property["model"]
        gva_tensor["input_shapes"] = input_shapes
        gva_tensor["input_types"] = input_types
        gva_tensor["input_names"] = ""
        gva_tensor["output_names"] = ""

    def do_generate_output(self):
        try:
            dst = Gst.Buffer.new()
            self.add_model_info(dst)

            # Input Gst.Buffer
            src = self.queued_buf
            mems = [src.get_memory(i) for i in range(src.n_memory())]

            # TODO: remove tensors limitation
            if len(self.input_tensors_info) != 1:
                raise RuntimeError("Input tensors size != 1")

            for mem in mems:
                res, map = mem.map(Gst.MapFlags.READ | Gst.MapFlags.WRITE)
                if not res:
                    raise RuntimeError("Unable to map gst buffer memory")

                input_tensor_info = self.input_tensors_info[0]

                if not input_tensor_info.shape:
                    raise RuntimeError(
                        "Input shape is empty. Unable to create tensor")

                nd_arr = np.ndarray(shape=input_tensor_info.shape,
                                    buffer=map.data, dtype=input_tensor_info.data_type)
                tensor = torch.Tensor(nd_arr, device=self.device).float(
                ).unsqueeze(0)  # FIXME: support batching

                with torch.no_grad():
                    output_tensor = self.model.forward(
                        tensor)[0]  # FIXME: support batching

                # Unmap input Gst.Memory
                mem.unmap(map)

                if isinstance(output_tensor, dict):
                    for tensor in output_tensor.values():
                        self.append_tensor_to_buffer(dst, tensor)
                elif isinstance(output_tensor, torch.Tensor):
                    self.append_tensor_to_buffer(dst, output_tensor)
                else:
                    raise RuntimeError(
                        f"Unsupported inference output type: '{type(output_tensor)}'")

            # Copy timestamps from input buffer
            dst.copy_into(src, Gst.BufferCopyFlags.TIMESTAMPS, 0, 0)
            # Push buffer downstream
            self.srcpad.push(dst)
        except Exception as exc:
            Gst.error(f"Error during generating output buffer: {exc}")
            traceback.print_exc()
            return Gst.FlowReturn.ERROR

        return Gst.FlowReturn.OK

    def append_tensor_to_buffer(self, buf: Gst.Buffer, tensor: torch.Tensor):
        tensor_nd_arr = tensor.numpy()

        mem = self.gst_alloc.alloc(tensor_nd_arr.nbytes)
        if not mem:
            raise RuntimeError(
                "Unable to allocate memory using default gst allocator")
        res, map = mem.map(Gst.MapFlags.WRITE)
        if not res:
            raise RuntimeError("Unable to map gst memory to write")

        np.copyto(np.ndarray(shape=tensor_nd_arr.shape,
                  buffer=map.data, dtype=tensor_nd_arr.dtype), tensor_nd_arr)
        mem.unmap(map)

        buf.append_memory(mem)


GObject.type_register(InferencePyTorch)

__gstelementfactory__ = ("pytorch_tensor_inference",
                         Gst.Rank.NONE, InferencePyTorch)
