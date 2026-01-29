# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import ctypes
from contextlib import contextmanager
import platform
import weakref
import gi
gi.require_version('GstVideo', '1.0')
gi.require_version('GstAudio', '1.0')
gi.require_version('Gst', '1.0')
from gi.repository import GstVideo, GstAudio, GObject, Gst

# libgstreamer
if platform.system() == 'Windows':
    libgst = ctypes.CDLL("gstreamer-1.0-0.dll")
else:
    libgst = ctypes.CDLL("libgstreamer-1.0.so.0")

GST_PADDING = 4
GST_VAAPI_VIDEO_MEMORY_NAME = "GstVaapiVideoMemory"


class GstMapInfo(ctypes.Structure):
    _fields_ = [("memory", ctypes.c_void_p),  # GstMemory *memory
                ("flags", ctypes.c_int),  # GstMapFlags flags
                ("data", ctypes.POINTER(ctypes.c_byte)),  # guint8 *data
                ("size", ctypes.c_size_t),  # gsize size
                ("maxsize", ctypes.c_size_t),  # gsize maxsize
                ("user_data", ctypes.c_void_p * 4),  # gpointer user_data[4]
                ("_gst_reserved", ctypes.c_void_p * GST_PADDING)]


GST_MAP_INFO_POINTER = ctypes.POINTER(GstMapInfo)


class GUnion(ctypes.Union):
    _fields_ = [('v_int', ctypes.c_int),
                ('v_uint', ctypes.c_uint),
                ('v_long', ctypes.c_long),
                ('v_ulong', ctypes.c_ulong),
                ('v_int64', ctypes.c_int64),
                ('v_uint64', ctypes.c_uint64),
                ('v_float', ctypes.c_float),
                ('v_double', ctypes.c_double),
                ('v_pointer', ctypes.c_void_p)]


class GValue(ctypes.Structure):
    _fields_ = [('g_type', ctypes.c_size_t),
                ('data', GUnion)]


G_VALUE_POINTER = ctypes.POINTER(GValue)


class GValueArray(ctypes.Structure):
    _fields_ = [("n_values", ctypes.c_uint32),
                ("values", ctypes.c_void_p),
                ("n_preallocated", ctypes.c_uint32)]


class GstMiniObject(ctypes.Structure):
    _fields_ = [
        ("type", ctypes.c_void_p),
        ("refcount", ctypes.c_int),
        ("lockstate", ctypes.c_int),
        ("flags", ctypes.c_uint)
    ]


class GstMemory(ctypes.Structure):
    _fields_ = [
        ("mini_object", GstMiniObject),
        ("allocator", ctypes.c_void_p),
        ("parent", ctypes.c_void_p),
        ("maxsize", ctypes.c_size_t),
        ("align", ctypes.c_size_t),
        ("offset", ctypes.c_size_t),
        ("size", ctypes.c_size_t)
    ]


GST_MEMORY_POINTER = ctypes.POINTER(GstMemory)
G_VALUE_ARRAY_POINTER = ctypes.POINTER(GValueArray)

# gst buffer
libgst.gst_buffer_map.argtypes = [
    ctypes.c_void_p, GST_MAP_INFO_POINTER, ctypes.c_int]
libgst.gst_buffer_map.restype = ctypes.c_int
libgst.gst_buffer_unmap.argtypes = [ctypes.c_void_p, GST_MAP_INFO_POINTER]
libgst.gst_buffer_unmap.restype = None
libgst.gst_buffer_iterate_meta_filtered.argtypes = [
    ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p]
libgst.gst_buffer_iterate_meta_filtered.restype = ctypes.c_void_p
libgst.gst_buffer_remove_meta.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
libgst.gst_buffer_remove_meta.restype = ctypes.c_bool
libgst.gst_buffer_add_meta.argtypes = [
    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
libgst.gst_buffer_add_meta.restype = ctypes.c_void_p
libgst.gst_buffer_get_memory.argtypes = [ctypes.c_void_p, ctypes.c_int]
libgst.gst_buffer_get_memory.restype = GST_MEMORY_POINTER

# gst memory
libgst.gst_memory_is_type.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
libgst.gst_memory_is_type.restype = ctypes.c_bool

# gst miniobject
libgst.gst_mini_object_make_writable.argtypes = [ctypes.c_void_p]
libgst.gst_mini_object_make_writable.restype = ctypes.c_void_p
libgst.gst_mini_object_is_writable.argtypes = [ctypes.c_void_p]
libgst.gst_mini_object_is_writable.restype = ctypes.c_int
libgst.gst_mini_object_ref.argtypes = [ctypes.c_void_p]
libgst.gst_mini_object_ref.restype = ctypes.c_void_p
libgst.gst_mini_object_unref.argtypes = [ctypes.c_void_p]
libgst.gst_mini_object_unref.restype = ctypes.c_void_p

# gst structure
libgst.gst_structure_get_name.argtypes = [ctypes.c_void_p]
libgst.gst_structure_get_name.restype = ctypes.c_char_p
libgst.gst_structure_has_name.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
libgst.gst_structure_has_name.restype = ctypes.c_bool
libgst.gst_structure_set_name.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
libgst.gst_structure_set_name.restypes = None
libgst.gst_structure_set_value.argtypes = [
    ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p]
libgst.gst_structure_set_value.restypes = None
libgst.gst_structure_set_array.argtypes = [
    ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p]
libgst.gst_structure_set_array.restypes = None
libgst.gst_structure_remove_field.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
libgst.gst_structure_remove_field.restypes = None
libgst.gst_structure_get_field_type.argtypes = [
    ctypes.c_void_p, ctypes.c_char_p]
libgst.gst_structure_get_field_type.restypes = ctypes.c_size_t
libgst.gst_structure_get_string.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
libgst.gst_structure_get_string.restype = ctypes.c_char_p
libgst.gst_structure_get_value.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
libgst.gst_structure_get_value.restype = ctypes.c_void_p
libgst.gst_structure_get_int.argtypes = [
    ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_int)]
libgst.gst_structure_get_int.restype = ctypes.c_int
libgst.gst_structure_get_double.argtypes = [
    ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_double)]
libgst.gst_structure_get_double.restype = ctypes.c_int
libgst.gst_structure_get_array.argtypes = [
    ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(G_VALUE_ARRAY_POINTER)]
libgst.gst_structure_get_array.restype = ctypes.c_bool
libgst.gst_structure_n_fields.argtypes = [ctypes.c_void_p]
libgst.gst_structure_n_fields.restype = ctypes.c_int
libgst.gst_structure_nth_field_name.argtypes = [ctypes.c_void_p, ctypes.c_uint]
libgst.gst_structure_nth_field_name.restype = ctypes.c_char_p
libgst.gst_structure_new_empty.argtypes = [ctypes.c_char_p]
libgst.gst_structure_new_empty.restype = ctypes.c_void_p
libgst.gst_structure_copy.argtypes = [ctypes.c_void_p]
libgst.gst_structure_copy.restype = ctypes.c_void_p
libgst.gst_structure_free.argtypes = [ctypes.c_void_p]
libgst.gst_structure_free.restype = None

# gst_caps
libgst.gst_caps_get_structure.argtypes = [ctypes.c_void_p, ctypes.c_uint]
libgst.gst_caps_get_structure.restype = ctypes.c_void_p

# gst_value_array
libgst.gst_value_array_get_type.argtypes = None
libgst.gst_value_array_get_type.restype = ctypes.c_void_p

libgst.gst_value_array_append_value.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
libgst.gst_value_array_append_value.restype = None

# gst_meta
libgst.gst_meta_get_info.argtypes = [ctypes.c_char_p]
libgst.gst_meta_get_info.restype = ctypes.c_void_p

# gst utils
libgst.gst_util_seqnum_next.restype = ctypes.c_uint

def gst_structure_free(ptr: int):
    if ptr:
        libgst.gst_structure_free(ctypes.c_void_p(ptr))


def is_vaapi_buffer(_buffer):
    if _buffer is None:
        raise TypeError("Passed buffer is None")
    mem = libgst.gst_buffer_get_memory(hash(_buffer), 0)
    if mem is None:
        return False
    res = libgst.gst_memory_is_type(
        mem, GST_VAAPI_VIDEO_MEMORY_NAME.encode('utf-8'))
    # gst_memory_unref
    mem.contents.mini_object.refcount -= 1
    return res


@contextmanager
def GST_PAD_PROBE_INFO_BUFFER(info):
    _buffer = info.get_buffer()
    _buffer.mini_object.refcount -= 1
    try:
        yield _buffer
    finally:
        _buffer.mini_object.refcount += 1


@contextmanager
def TRANSFORM_IP_BUFFER(_buffer):
    _buffer.mini_object.refcount -= 1
    try:
        yield _buffer
    finally:
        _buffer.mini_object.refcount += 1


@contextmanager
def gst_buffer_data(_buffer, flags):
    if _buffer is None:
        raise TypeError("Cannot pass NULL to gst_buffer_map")

    ptr = hash(_buffer)

    # Prevent calling gst_buffer_map with VASurface buffer for writing
    # Otherwise it will fail silently
    if (flags & Gst.MapFlags.WRITE) and is_vaapi_buffer(ptr):
        raise RuntimeError("Couldn't map VASurface buffer for writing")

    mapping = GstMapInfo()
    success = libgst.gst_buffer_map(ptr, mapping, flags)

    if not success:
        raise RuntimeError("Couldn't map buffer")

    try:
        yield ctypes.cast(mapping.data, ctypes.POINTER(ctypes.c_byte * mapping.size)).contents
    finally:
        libgst.gst_buffer_unmap(ptr, mapping)
        
class GstStructureHandle:
    def __init__(self, ptr: ctypes.c_void_p):
        addr = int(ptr)
        self.ptr = addr
        self._finalizer = weakref.finalize(self, gst_structure_free, addr)

    def as_c_void_p(self) -> ctypes.c_void_p:
        return ctypes.c_void_p(self.ptr)


# libgobject
if platform.system() == 'Windows':
    libgobject = ctypes.CDLL("gobject-2.0-0.dll")
else:
    libgobject = ctypes.CDLL("libgobject-2.0.so.0")


class GList(ctypes.Structure):
    pass


GLIST_POINTER = ctypes.POINTER(GList)

GList._fields_ = [
    ('data', ctypes.c_void_p),
    ('next', GLIST_POINTER),
    ('prev', GLIST_POINTER)
]


libgobject.g_type_name.argtypes = [ctypes.c_void_p]
libgobject.g_type_name.restype = ctypes.c_char_p
libgobject.g_type_from_name.argtypes = [ctypes.c_char_p]
libgobject.g_type_from_name.restype = ctypes.c_ulong
libgobject.g_value_get_variant.argtypes = [ctypes.c_void_p]
libgobject.g_value_get_variant.restype = ctypes.c_void_p
libgobject.g_value_get_int.argtypes = [ctypes.c_void_p]
libgobject.g_value_get_int.restype = ctypes.c_void_p
libgobject.g_value_set_variant.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
libgobject.g_value_set_variant.restype = None
libgobject.g_value_array_new.argtypes = [ctypes.c_size_t]
libgobject.g_value_array_new.restype = G_VALUE_ARRAY_POINTER
libgobject.g_value_init.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
libgobject.g_value_init.restype = ctypes.c_void_p
libgobject.g_value_set_uint.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
libgobject.g_value_set_uint.restype = ctypes.c_void_p
libgobject.g_value_set_int.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
libgobject.g_value_set_int.restype = ctypes.c_void_p
libgobject.g_value_array_append.argtypes = [G_VALUE_ARRAY_POINTER,  ctypes.c_void_p]
libgobject.g_value_array_append.restype = G_VALUE_ARRAY_POINTER
libgobject.g_value_array_get_nth.argtypes = [G_VALUE_ARRAY_POINTER, ctypes.c_uint]
libgobject.g_value_array_get_nth.restype = G_VALUE_POINTER
libgobject.g_value_get_uint.argtypes = [G_VALUE_POINTER]
libgobject.g_value_get_uint.restype = ctypes.c_uint
libgobject.g_value_get_float.argtypes = [G_VALUE_POINTER]
libgobject.g_value_get_float.restype = ctypes.c_float

# libglib
if platform.system() == 'Windows':
    libglib = ctypes.CDLL("glib-2.0-0.dll")
else:
    libglib = ctypes.CDLL("libglib-2.0.so.0")
libglib.g_strdup.argtypes = [ctypes.c_char_p]
libglib.g_strdup.restype = ctypes.c_void_p
libglib.g_variant_get_fixed_array.argtypes = [
    ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.c_size_t]
libglib.g_variant_get_fixed_array.restype = ctypes.c_void_p
libglib.g_variant_new_fixed_array.argtypes = [
    ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t]
libglib.g_variant_new_fixed_array.restype = ctypes.c_void_p
libglib.g_list_remove.argtypes = [GLIST_POINTER, ctypes.c_void_p]
libglib.g_list_remove.restype = GLIST_POINTER

# libgstvideo
if platform.system() == 'Windows':
    libgstvideo = ctypes.CDLL("gstvideo-1.0-0.dll")
else:
    libgstvideo = ctypes.CDLL("libgstvideo-1.0.so.0")

# VideoRegionOfInterestMeta
class VideoRegionOfInterestMeta(ctypes.Structure):
    _fields_ = [
        ('_meta_flags', ctypes.c_int),
        ('_info', ctypes.c_void_p),
        ('roi_type', ctypes.c_int),
        ('id', ctypes.c_int),
        ('parent_id', ctypes.c_int),
        ('x', ctypes.c_int),
        ('y', ctypes.c_int),
        ('w', ctypes.c_int),
        ('h', ctypes.c_int),
        ('_params', GLIST_POINTER)
    ]


VIDEO_REGION_OF_INTEREST_POINTER = ctypes.POINTER(VideoRegionOfInterestMeta)
libgstvideo.gst_video_region_of_interest_meta_get_param.argtypes = [
    VIDEO_REGION_OF_INTEREST_POINTER, ctypes.c_char_p]
libgstvideo.gst_video_region_of_interest_meta_get_param.restype = ctypes.c_void_p
libgstvideo.gst_video_region_of_interest_meta_add_param.argtypes = [
    VIDEO_REGION_OF_INTEREST_POINTER, ctypes.c_void_p]
libgstvideo.gst_video_region_of_interest_meta_add_param.restype = None
libgstvideo.gst_buffer_get_video_region_of_interest_meta_id.argtypes = [
    ctypes.c_void_p, ctypes.c_uint]
libgstvideo.gst_buffer_get_video_region_of_interest_meta_id.restype = ctypes.c_void_p

# GVATensorMeta
class GVATensorMeta(ctypes.Structure):
    _fields_ = [
        ('_meta_flags', ctypes.c_int),
        ('_info', ctypes.c_void_p),
        ('data', ctypes.c_void_p)
    ]

    @classmethod
    def add_tensor_meta(cls, buffer):
        try:
            tensor_meta_info = libgst.gst_meta_get_info("GstGVATensorMeta".encode('utf-8'))
            value = libgst.gst_buffer_add_meta(hash(buffer), tensor_meta_info, None)
        except Exception as error:
            value = None

        if not value:
            return

        return ctypes.cast(value, ctypes.POINTER(GVATensorMeta)).contents


class GVAJSONMetaStr(str):
    def __new__(cls, meta, content):
        return super().__new__(cls, content)

    def __init__(self, meta, content):
        self.meta = meta
        super().__init__()


# GVAJSONMeta
class GVAJSONMeta(ctypes.Structure):
    _fields_ = [('_meta_flags', ctypes.c_int),
                ('_info', ctypes.c_void_p),
                ('_message', ctypes.c_char_p)
                ]

    def get_message(self):
        return GVAJSONMetaStr(self, self._message.decode('utf-8'))

    @classmethod
    def remove_json_meta(cls, buffer, meta):
        return libgst.gst_buffer_remove_meta(hash(buffer), ctypes.byref(meta))

    @classmethod
    def add_json_meta(cls, buffer, message):
        try:
            json_meta_info = libgst.gst_meta_get_info("GstGVAJSONMeta".encode('utf-8'))
            value = libgst.gst_buffer_add_meta(hash(buffer), json_meta_info, None)
        except Exception as error:
            value = None

        if value is None:
            return

        meta = ctypes.cast(value, ctypes.POINTER(GVAJSONMeta)).contents
        meta._message = libglib.g_strdup(message.encode('utf-8'))
        return meta

    @classmethod
    def iterate(cls, buffer):
        try:
            meta_api = hash(GObject.GType.from_name("GstGVAJSONMetaAPI"))
        except:
            return
        gpointer = ctypes.c_void_p()
        while(True):
            try:
                value = libgst.gst_buffer_iterate_meta_filtered(
                    hash(buffer), ctypes.byref(gpointer), meta_api)
            except Exception as error:
                value = None

            if not value:
                return

            yield ctypes.cast(value, ctypes.POINTER(GVAJSONMeta)).contents

def _VideoInfoFromCaps(caps):
    return GstVideo.VideoInfo.new_from_caps(caps)

def _VideoInfoFromCaps_Legacy(caps):
    video_info = GstVideo.VideoInfo()
    video_info.from_caps(caps)
    return video_info

def _AudioInfoFromCaps(caps):
    return GstAudio.AudioInfo.new_from_caps(caps)

def _AudioInfoFromCaps_Legacy(caps):
    audio_info = GstAudio.AudioInfo()
    audio_info.from_caps(caps)
    return audio_info

# Check for GST 1.20 APIs
# pylint: disable=method-hidden
VideoInfoFromCaps = _VideoInfoFromCaps_Legacy
if hasattr(GstVideo.VideoInfo, 'new_from_caps'):
    VideoInfoFromCaps = _VideoInfoFromCaps

AudioInfoFromCaps = _AudioInfoFromCaps_Legacy
if hasattr(GstAudio.AudioInfo, 'new_from_caps'):
    AudioInfoFromCaps = _AudioInfoFromCaps
