# ==============================================================================
# Copyright (C) 2018-2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================

import ctypes
from ..util import GLIST_POINTER

# AudioEventMeta
class AudioEventMeta(ctypes.Structure):
    _fields_ = [
        ('_meta_flags', ctypes.c_int),
        ('_info', ctypes.c_void_p),
        ('event_type', ctypes.c_int),
        ('id', ctypes.c_int),
        ('start_timestamp', ctypes.c_uint64),
        ('end_timestamp', ctypes.c_uint64),
        ('_params', GLIST_POINTER)
    ]
