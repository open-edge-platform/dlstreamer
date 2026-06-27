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
 * GstAnalytics3DODMtd:
 *
 * Handle containing data required to use gst_analytics_3d_od_mtd APIs.
 */
typedef struct _GstAnalyticsMtd GstAnalytics3DODMtd;

/**
 * GstAnalytics3DSensorModality:
 * @GST_ANALYTICS_3D_SENSOR_LIDAR:  Detection sourced from a LiDAR-based detector
 * @GST_ANALYTICS_3D_SENSOR_RADAR:  Detection sourced from a radar-based detector
 */
typedef enum { GST_ANALYTICS_3D_SENSOR_LIDAR = 0, GST_ANALYTICS_3D_SENSOR_RADAR = 1 } GstAnalytics3DSensorModality;

DLS_EXPORT GstAnalyticsMtdType gst_analytics_3d_od_mtd_get_mtd_type(void);

DLS_EXPORT gboolean gst_analytics_relation_meta_add_3d_od_mtd(GstAnalyticsRelationMeta *instance, gfloat x, gfloat y,
                                                              gfloat z, gfloat length, gfloat width, gfloat height,
                                                              gfloat yaw, gfloat pitch, gfloat roll, gint class_id,
                                                              gfloat confidence, GstAnalytics3DSensorModality modality,
                                                              GstAnalytics3DODMtd *mtd);

DLS_EXPORT gboolean gst_analytics_3d_od_mtd_get_location(const GstAnalytics3DODMtd *instance, gfloat *x, gfloat *y,
                                                         gfloat *z, gfloat *length, gfloat *width, gfloat *height,
                                                         gfloat *yaw, gfloat *pitch, gfloat *roll);

DLS_EXPORT gboolean gst_analytics_3d_od_mtd_get_class(const GstAnalytics3DODMtd *instance, gint *class_id,
                                                      gfloat *confidence);

DLS_EXPORT gboolean gst_analytics_3d_od_mtd_get_modality(const GstAnalytics3DODMtd *instance,
                                                         GstAnalytics3DSensorModality *modality);

DLS_EXPORT gboolean gst_analytics_relation_meta_get_3d_od_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                              GstAnalytics3DODMtd *rlt);

G_END_DECLS
