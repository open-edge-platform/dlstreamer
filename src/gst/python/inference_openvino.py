#!/usr/bin/python3

# ==============================================================================
# Copyright (C) 2022-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import numpy as np

from openvino.runtime import Core, Layout, Type, InferRequest, AsyncInferQueue
from openvino.preprocess import PrePostProcessor

import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstBase', '1.0')
gi.require_version('GstVideo', '1.0')
from gi.repository import Gst, GObject, GLib, GstBase, GstVideo # pylint: disable=no-name-in-module, wrong-import-position

Gst.init(None)

TENSORS_CAPS = Gst.Caps.from_string("other/tensors")


class InferenceOpenVINO(GstBase.BaseTransform):
    __gstmetadata__ = ('OpenVINO inference', 'Transform',
                       'OpenVINO™ toolkit inference element', 'dkl')

    __gsttemplates__ = (Gst.PadTemplate.new("sink", Gst.PadDirection.SINK, Gst.PadPresence.ALWAYS, TENSORS_CAPS),
                        Gst.PadTemplate.new("src", Gst.PadDirection.SRC, Gst.PadPresence.ALWAYS, TENSORS_CAPS))

    __gproperties__ = {
        "model": (GObject.TYPE_STRING, "model", "OpenVINO™ toolkit model path", "", GObject.ParamFlags.READWRITE),
        "device": (GObject.TYPE_STRING, "device", "Inference device", "CPU", GObject.ParamFlags.READWRITE),
        "nireq": (GObject.TYPE_INT64, "nireq", "Number inference requests", 0, GLib.MAXINT, 0, GObject.ParamFlags.READWRITE),
    }

    def __init__(self, gproperties=__gproperties__):
        super(InferenceOpenVINO, self).__init__()

        self.property = {}  # default values
        for key, value in gproperties.items():
            self.property[key] = value[3] if value[0] in (
                bool, str, GObject.TYPE_STRING, GObject.TYPE_BOOLEAN) else value[5]

        self.core = Core()
        self.model = None
        self.compiled_model = None
        self.infer_queue = None

    def do_set_property(self, prop: GObject.GParamSpec, value):
        self.property[prop.name] = value

    def do_get_property(self, prop: GObject.GParamSpec):
        return self.property[prop.name]

    def read_model(self):
        if not self.model:
            model = self.core.read_model(self.property['model'])
            ppp = PrePostProcessor(model)
            ppp.input().tensor().set_layout(Layout('NHWC')).set_element_type(Type.u8)
            self.model = ppp.build()

    def compile_model(self):
        if not self.compiled_model:
            self.compiled_model = self.core.compile_model(
                self.model, self.property['device'])
            if self.property['nireq'] != 1:
                self.infer_queue = AsyncInferQueue(
                    self.compiled_model, self.property['nireq'])
                self.infer_queue.set_callback(self.completion_callback)

    def do_transform_caps(self, direction, caps, filter):
        self.read_model()
        infos = self.model.inputs if direction == Gst.PadDirection.SRC else self.model.outputs
        shapes = [":".join(str(d) for d in np.array(info.shape)[::-1])
                  for info in infos]  # dims in reverse order
        types = [self.TYPE_NAME[info.element_type.get_type_name()]
                 for info in infos]
        caps_str = 'other/tensors,num_tensors=(uint){num},types={types},dimensions={shapes}'.format(
            num=len(infos), types=",".join(types), shapes=",".join(shapes))
        my_caps = Gst.Caps.from_string(caps_str)
        if filter:
            my_caps = my_caps.intersect(filter)
        return my_caps

    def do_set_caps(self, incaps, outcaps):
        self.compile_model()
        return True

    def do_generate_output(self):
        # Input Gst.Buffer
        src = self.queued_buf

        # Map all Gst.Memory to numpy arrays
        mems = [src.get_memory(i) for i in range(src.n_memory())]
        maps = [mem.map(Gst.MapFlags.READ)[1] for mem in mems]
        tensors = [np.ndarray(shape=info.shape, buffer=map.data, dtype=np.uint8)
                   for map, info in zip(maps, self.model.inputs)]

        # Submit async inference request or run inference synchronously
        if self.infer_queue:
            self.infer_queue.start_async(tensors, (src, mems, maps))
        else:
            results = self.compiled_model.infer_new_request(tensors)
            self.push_results(src, mems, maps, results.values())

        # Return GST_BASE_TRANSFORM_FLOW_DROPPED as we push buffer in function push_results()
        return Gst.FlowReturn.CUSTOM_SUCCESS

    def completion_callback(self, infer_request, args):
        (src, mems, maps) = args
        self.push_results(src, mems, maps, infer_request.results.values())

    def push_results(self, src, mems, maps, tensors):
        # Unmap input Gst.Memory
        for mem, map in zip(mems, maps):
            mem.unmap(map)

        # Wrap output tensors into Gst.Memory and attach to Gst.Buffer
        dst = Gst.Buffer.new()
        for tensor in tensors:
            mem = Gst.Memory.new_wrapped(
                0, tensor.tobytes(), tensor.nbytes, 0, None, None)
            dst.append_memory(mem)

        # Copy timestamps from input buffer
        dst.copy_into(src, Gst.BufferCopyFlags.TIMESTAMPS, 0, 0)

        # Push buffer downstream
        self.srcpad.push(dst)

    def do_sink_event(self, event):
        if (event.type == Gst.EventType.EOS or event.type == Gst.EventType.FLUSH_STOP) and self.infer_queue:
            self.infer_queue.wait_all()
        return GstBase.BaseTransform.do_sink_event(self, event)

    def do_stop(self):
        if self.infer_queue:
            self.infer_queue.wait_all()
        return True

    TYPE_NAME = {
        "u8": "uint8",
        "f32": "float32"
    }


GObject.type_register(InferenceOpenVINO)

__gstelementfactory__ = ("inference_openvino",
                         Gst.Rank.NONE, InferenceOpenVINO)
