/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef GVA_UTILS_H
#define GVA_UTILS_H

#include "glib.h"
#include <gst/analytics/analytics.h>
#include <gst/gst.h>
#include <gst/gstbuffer.h>
#include <gst/video/gstvideometa.h>

G_BEGIN_DECLS

gboolean get_object_id(GstVideoRegionOfInterestMeta *meta, int *id);
void set_object_id(GstVideoRegionOfInterestMeta *meta, gint id);

gboolean get_od_id(GstAnalyticsODMtd od_mtd, int *id);
void set_od_id(GstAnalyticsODMtd od_mtd, gint id);

void gva_buffer_check_and_make_writable(GstBuffer **buffer, const char *called_function_name);

G_END_DECLS

#define GVA_AGGREGATED_ROI_TYPE "dlstreamer-aggregated-roi"

#define GST_VIDEO_REGION_OF_INTEREST_META_ITERATE(buf, state)                                                          \
    ((GstVideoRegionOfInterestMeta *)gst_buffer_iterate_meta_filtered(buf, state,                                      \
                                                                      GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE))

#endif // GVA_UTILS_H
