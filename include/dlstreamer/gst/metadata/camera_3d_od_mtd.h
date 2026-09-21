/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "gva_export.h"
#include <gst/analytics/gstanalyticsmeta.h>
#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * GstAnalyticsCamera3DODMtd:
 *
 * Handle containing data required to use gst_analytics_camera_3d_od_mtd APIs.
 */
typedef struct _GstAnalyticsMtd GstAnalyticsCamera3DODMtd;

DLS_EXPORT GstAnalyticsMtdType gst_analytics_camera_3d_od_mtd_get_mtd_type(void);

DLS_EXPORT gboolean gst_analytics_relation_meta_add_camera_3d_od_mtd(
    GstAnalyticsRelationMeta *instance, gint class_id, gfloat confidence, gfloat x1, gfloat y1, gfloat x2, gfloat y2,
    gfloat x, gfloat y, gfloat z, gfloat height, gfloat width, gfloat length, gfloat rotation_y, gfloat alpha,
    GstAnalyticsCamera3DODMtd *mtd);

DLS_EXPORT gboolean gst_analytics_camera_3d_od_mtd_get_class(const GstAnalyticsCamera3DODMtd *instance, gint *class_id,
                                                             gfloat *confidence);

DLS_EXPORT gboolean gst_analytics_camera_3d_od_mtd_get_box2d(const GstAnalyticsCamera3DODMtd *instance, gfloat *x1,
                                                             gfloat *y1, gfloat *x2, gfloat *y2);

DLS_EXPORT gboolean gst_analytics_camera_3d_od_mtd_get_location(const GstAnalyticsCamera3DODMtd *instance, gfloat *x,
                                                                gfloat *y, gfloat *z, gfloat *height, gfloat *width,
                                                                gfloat *length, gfloat *rotation_y, gfloat *alpha);

DLS_EXPORT gboolean gst_analytics_relation_meta_get_camera_3d_od_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                                     GstAnalyticsCamera3DODMtd *rlt);

G_END_DECLS
