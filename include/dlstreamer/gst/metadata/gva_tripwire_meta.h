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
 * GstAnalyticsTripwireMtd:
 * @id: Instance identifier.
 * @meta: Instance of #GstAnalyticsRelationMeta where this metadata is stored.
 *
 * Handle to tripwire analytics metadata.
 * Provides access to tripwire crossing information (tripwire_id, direction).
 */
typedef struct _GstAnalyticsMtd GstAnalyticsTripwireMtd;

/**
 * gst_analytics_tripwire_mtd_get_mtd_type:
 *
 * Returns: The metadata type ID for GstAnalyticsTripwireMtd.
 */
DLS_EXPORT GstAnalyticsMtdType gst_analytics_tripwire_mtd_get_mtd_type(void);

/**
 * gst_analytics_tripwire_mtd_get_info:
 * @handle: A #GstAnalyticsTripwireMtd handle.
 * @tripwire_id: (out) (transfer full) (nullable): Tripwire identifier string.
 * @direction: (out) (nullable): Crossing direction (-1 backward, 0 undefined, 1 forward).
 *
 * Retrieves tripwire-specific information from the tripwire metadata.
 *
 * Returns: TRUE if the tripwire data was successfully retrieved, FALSE otherwise.
 */
DLS_EXPORT gboolean gst_analytics_tripwire_mtd_get_info(const GstAnalyticsTripwireMtd *handle, gchar **tripwire_id,
                                                        gint *direction);

/**
 * gst_analytics_relation_meta_add_tripwire_mtd:
 * @relation_meta: A #GstAnalyticsRelationMeta instance.
 * @tripwire_id: Tripwire identifier.
 * @direction: Crossing direction (-1 backward, 0 undefined, 1 forward).
 * @tripwire_mtd: (out): Pointer to #GstAnalyticsTripwireMtd to be filled.
 *
 * Adds tripwire crossing metadata as a relation in the analytics metadata.
 * Creates a queryable OD → Tripwire relationship.
 *
 * Returns: TRUE if tripwire metadata was successfully added, FALSE otherwise.
 */
DLS_EXPORT gboolean gst_analytics_relation_meta_add_tripwire_mtd(GstAnalyticsRelationMeta *relation_meta,
                                                                 const gchar *tripwire_id, gint direction,
                                                                 GstAnalyticsTripwireMtd *tripwire_mtd);

/**
 * gst_analytics_relation_meta_get_tripwire_mtd:
 * @meta: A #GstAnalyticsRelationMeta instance.
 * @an_meta_id: Id of the tripwire metadata to retrieve.
 * @rlt: (out): Pointer to #GstAnalyticsTripwireMtd to be filled.
 *
 * Retrieves tripwire metadata by its ID from the analytics relation metadata.
 *
 * Returns: TRUE if the tripwire metadata was found and @rlt was filled, FALSE otherwise.
 */
DLS_EXPORT gboolean gst_analytics_relation_meta_get_tripwire_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                                 GstAnalyticsTripwireMtd *rlt);

G_END_DECLS
