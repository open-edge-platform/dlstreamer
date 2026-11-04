/*******************************************************************************
 * Copyright (C) 2020-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gva_caps.h"

CapsFeature get_caps_feature(GstCaps *caps) {
    for (size_t i = 0; i < gst_caps_get_size(caps); i++) {
        GstCapsFeatures *const features = gst_caps_get_features(caps, i);
        if (gst_caps_features_contains(features, DMABUF_FEATURE_STR))
            return DMA_BUF_CAPS_FEATURE;
        if (gst_caps_features_contains(features, VASURFACE_FEATURE_STR))
            return VA_SURFACE_CAPS_FEATURE;
        if (gst_caps_features_contains(features, VAMEMORY_FEATURE_STR))
            return VA_MEMORY_CAPS_FEATURE;
        if (gst_caps_features_contains(features, D3D11MEMORY_FEATURE_STR))
            return D3D11_MEMORY_CAPS_FEATURE;
        if (gst_caps_features_contains(features, D3D12MEMORY_FEATURE_STR))
            return D3D12_MEMORY_CAPS_FEATURE;
    }
    return SYSTEM_MEMORY_CAPS_FEATURE;
}