/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gva_caps.h"

// Preferred format to use with VASurface and DMABuf
#define WATERMARK_PREFERRED_REMOTE_FORMAT "RGBA"
// Preferred format to use with VAMemory and DMABuf
#define WATERMARK_VA_PREFERRED_REMOTE_FORMAT "NV12"

#define WATERMARK_SYSTEM_CAPS GST_VIDEO_CAPS_MAKE("{ BGRx, RGBA, BGRA, BGR, NV12, I420 }") "; "

#ifdef ENABLE_VAAPI
#define WATERMARK_VASURFACE_CAPS                                                                                       \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(VASURFACE_FEATURE_STR, WATERMARK_PREFERRED_REMOTE_FORMAT) "; "
#define WATERMARK_VAMEMORY_CAPS                                                                                        \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(VAMEMORY_FEATURE_STR, WATERMARK_VA_PREFERRED_REMOTE_FORMAT) "; "
#define WATERMARK_DMA_BUFFER_CAPS                                                                                      \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(DMABUF_FEATURE_STR, WATERMARK_VA_PREFERRED_REMOTE_FORMAT) "; "
#else
#define WATERMARK_VASURFACE_CAPS
#define WATERMARK_DMA_BUFFER_CAPS
#endif

#if _WIN32
#define WATERMARK_D3D11_CAPS                                                                                           \
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(D3D11MEMORY_FEATURE_STR, "{ BGRx, RGBA, BGRA, BGR, NV12, I420 }") "; "
#else
#define WATERMARK_D3D11_CAPS
#endif

#define WATERMARK_ALL_CAPS                                                                                             \
    WATERMARK_SYSTEM_CAPS WATERMARK_VASURFACE_CAPS WATERMARK_DMA_BUFFER_CAPS WATERMARK_VAMEMORY_CAPS                   \
        WATERMARK_D3D11_CAPS
