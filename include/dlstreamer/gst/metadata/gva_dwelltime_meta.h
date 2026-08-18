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
 * GstAnalyticsDwellTimeMtd:
 * @id: Instance identifier.
 * @meta: Instance of #GstAnalyticsRelationMeta where this metadata is stored.
 *
 * Handle to dwell-time analytics metadata.
 * Carries zone_id, dwell_time (seconds elapsed in zone), and first_seen_timestamp.
 */
typedef struct _GstAnalyticsMtd GstAnalyticsDwellTimeMtd;

/**
 * gst_analytics_dwelltime_mtd_get_mtd_type:
 *
 * Returns: The metadata type ID for GstAnalyticsDwellTimeMtd.
 */
DLS_EXPORT GstAnalyticsMtdType gst_analytics_dwelltime_mtd_get_mtd_type(void);

/**
 * gst_analytics_dwelltime_mtd_get_info:
 * @handle: A #GstAnalyticsDwellTimeMtd handle.
 * @zone_id: (out) (transfer full) (nullable): Zone identifier string.
 * @dwell_time: (out) (nullable): Seconds the object has been inside the zone.
 * @first_seen_timestamp: (out) (nullable): Buffer PTS (seconds) when the object first entered the zone.
 *
 * Retrieves dwell-time information from the metadata.
 *
 * Returns: TRUE if the data was successfully retrieved, FALSE otherwise.
 */
DLS_EXPORT gboolean gst_analytics_dwelltime_mtd_get_info(const GstAnalyticsDwellTimeMtd *handle, gchar **zone_id,
                                                         gdouble *dwell_time, gdouble *first_seen_timestamp);

/**
 * gst_analytics_relation_meta_add_dwelltime_mtd:
 * @relation_meta: A #GstAnalyticsRelationMeta instance.
 * @zone_id: Zone identifier.
 * @dwell_time: Seconds the object has been inside the zone.
 * @first_seen_timestamp: Buffer PTS (seconds) when the object first entered the zone.
 * @dwelltime_mtd: (out): Pointer to #GstAnalyticsDwellTimeMtd to be filled.
 *
 * Adds dwell-time metadata to the analytics relation metadata.
 *
 * Returns: TRUE if the metadata was successfully added, FALSE otherwise.
 */
DLS_EXPORT gboolean gst_analytics_relation_meta_add_dwelltime_mtd(GstAnalyticsRelationMeta *relation_meta,
                                                                  const gchar *zone_id, gdouble dwell_time,
                                                                  gdouble first_seen_timestamp,
                                                                  GstAnalyticsDwellTimeMtd *dwelltime_mtd);

/**
 * gst_analytics_relation_meta_get_dwelltime_mtd:
 * @meta: A #GstAnalyticsRelationMeta instance.
 * @an_meta_id: Id of the dwell-time metadata to retrieve.
 * @rlt: (out): Pointer to #GstAnalyticsDwellTimeMtd to be filled.
 *
 * Retrieves dwell-time metadata by its ID from the analytics relation metadata.
 *
 * Returns: TRUE if the metadata was found and @rlt was filled, FALSE otherwise.
 */
DLS_EXPORT gboolean gst_analytics_relation_meta_get_dwelltime_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                                  GstAnalyticsDwellTimeMtd *rlt);

G_END_DECLS
