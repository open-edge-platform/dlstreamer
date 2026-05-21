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
 * GstAnalyticsZoneMtd:
 * @id: Instance identifier.
 * @meta: Instance of #GstAnalyticsRelationMeta where this metadata is stored.
 *
 * Handle to zone analytics metadata.
 * Provides access to zone-related information (zone_id).
 */
typedef struct _GstAnalyticsMtd GstAnalyticsZoneMtd;

/**
 * gst_analytics_zone_mtd_get_mtd_type:
 *
 * Returns: The metadata type ID for GstAnalyticsZoneMtd.
 */
DLS_EXPORT GstAnalyticsMtdType gst_analytics_zone_mtd_get_mtd_type(void);

/**
 * gst_analytics_zone_mtd_get_info:
 * @handle: A #GstAnalyticsZoneMtd handle.
 * @zone_id: (out) (transfer full) (nullable): Zone identifier string.
 *
 * Retrieves zone-specific information from the zone metadata.
 *
 * Returns: TRUE if the zone data was successfully retrieved, FALSE otherwise.
 */
DLS_EXPORT gboolean gst_analytics_zone_mtd_get_info(const GstAnalyticsZoneMtd *handle, gchar **zone_id);

/**
 * gst_analytics_relation_meta_add_zone_mtd:
 * @relation_meta: A #GstAnalyticsRelationMeta instance.
 * @zone_id: Zone identifier.
 * @zone_mtd: (out): Pointer to #GstAnalyticsZoneMtd to be filled.
 *
 * Adds zone metadata to the analytics relation metadata.
 *
 * Returns: TRUE if zone metadata was successfully added, FALSE otherwise.
 */
DLS_EXPORT gboolean gst_analytics_relation_meta_add_zone_mtd(GstAnalyticsRelationMeta *relation_meta,
                                                             const gchar *zone_id, GstAnalyticsZoneMtd *zone_mtd);

/**
 * gst_analytics_relation_meta_get_zone_mtd:
 * @meta: A #GstAnalyticsRelationMeta instance.
 * @an_meta_id: Id of the zone metadata to retrieve.
 * @rlt: (out): Pointer to #GstAnalyticsZoneMtd to be filled.
 *
 * Retrieves zone metadata by its ID from the analytics relation metadata.
 *
 * Returns: TRUE if the zone metadata was found and @rlt was filled, FALSE otherwise.
 */
DLS_EXPORT gboolean gst_analytics_relation_meta_get_zone_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                             GstAnalyticsZoneMtd *rlt);

G_END_DECLS
