/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "config.h"

#include "dlstreamer/gst/plugin.h"
#include "gva_tensor_meta.h"
#include <gst/gst.h>

#include "batch_create.h"
#include "batch_split.h"
#include "capsrelax.h"
#include "gvadrop.h"
#include "meta_aggregate.h"
#include "meta_smooth.h"
#include "roi_split.h"
#include "video_frames_buffer.h"

#include "gvainference.h"

static gboolean plugin_init(GstPlugin *plugin) {
    if (!gst_element_register(plugin, "batch_create", GST_RANK_NONE, batch_create_get_type()))
        return FALSE;
    if (!gst_element_register(plugin, "batch_split", GST_RANK_NONE, batch_split_get_type()))
        return FALSE;
    if (!gst_element_register(plugin, "capsrelax", GST_RANK_NONE, gst_capsrelax_get_type()))
        return FALSE;
    if (!gst_element_register(plugin, "gvadrop", GST_RANK_NONE, GST_TYPE_GVA_DROP))
        return FALSE;
    if (!gst_element_register(plugin, "meta_aggregate", GST_RANK_NONE, meta_aggregate_get_type()))
        return FALSE;
    if (!gst_element_register(plugin, "roi_split", GST_RANK_NONE, roi_split_get_type()))
        return FALSE;
    if (!gst_element_register(plugin, "meta_smooth", GST_RANK_NONE, meta_smooth_get_type()))
        return FALSE;
    if (!gst_element_register(plugin, "video_frames_buffer", GST_RANK_NONE, video_frames_buffer_get_type()))
        return FALSE;

    // Legacy elements
    if (!gst_element_register(plugin, "gvainference2", GST_RANK_NONE, gva_inference_get_type()))
        return FALSE;

    return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, dlstreamer_elements,
                  PRODUCT_FULL_NAME " elements implemented directly on GStreamer API", plugin_init, PLUGIN_VERSION,
                  PLUGIN_LICENSE, PACKAGE_NAME, GST_PACKAGE_ORIGIN)
