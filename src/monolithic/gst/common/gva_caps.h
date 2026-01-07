/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "config.h"

#include <gst/gstcaps.h>
#include <gst/video/video.h>

#define DMABUF_FEATURE_STR "memory:DMABuf"
#define VASURFACE_FEATURE_STR "memory:VASurface"
#define VAMEMORY_FEATURE_STR "memory:VAMemory"
#define D3D11MEMORY_FEATURE_STR "memory:D3D11Memory"

#define SYSTEM_MEM_CAPS GST_VIDEO_CAPS_MAKE("{ BGRx, BGRA, BGR, NV12, I420 }") "; "

#if defined(ENABLE_VAAPI) && !defined(_WIN32)
#define VASURFACE_CAPS GST_VIDEO_CAPS_MAKE_WITH_FEATURES(VASURFACE_FEATURE_STR, "{ NV12 }") "; "
#define VAMEMORY_CAPS GST_VIDEO_CAPS_MAKE_WITH_FEATURES(VAMEMORY_FEATURE_STR, "{ NV12 }") "; "
#define DMA_BUFFER_CAPS GST_VIDEO_CAPS_MAKE_WITH_FEATURES(DMABUF_FEATURE_STR, "{ DMA_DRM }") "; "
#else
#define VASURFACE_CAPS
#define VAMEMORY_CAPS
#define DMA_BUFFER_CAPS
#endif

#ifdef _WIN32
#define D3D11MEMORY_CAPS GST_VIDEO_CAPS_MAKE_WITH_FEATURES(D3D11MEMORY_FEATURE_STR, "{ NV12,BGRA }") "; "
#else
#define D3D11MEMORY_CAPS
#endif

#define GVA_CAPS SYSTEM_MEM_CAPS DMA_BUFFER_CAPS VASURFACE_CAPS VAMEMORY_CAPS D3D11MEMORY_CAPS
typedef enum {
    SYSTEM_MEMORY_CAPS_FEATURE,
    VA_SURFACE_CAPS_FEATURE,
    VA_MEMORY_CAPS_FEATURE,
    DMA_BUF_CAPS_FEATURE,
    D3D11_MEMORY_CAPS_FEATURE,
    ANY_CAPS_FEATURE
} CapsFeature;

G_BEGIN_DECLS

CapsFeature get_caps_feature(GstCaps *caps);

G_END_DECLS
