/* GStreamer
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 * gstanalyticssemantictagmtd.h
 */

#ifndef __GST_ANALYTICS_SEMANTIC_TAG_MTD_H__
#define __GST_ANALYTICS_SEMANTIC_TAG_MTD_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/analytics/analytics-meta-prelude.h>
#include <gst/analytics/gstanalyticsmeta.h>
#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * GstAnalyticsSemanticTagMtd:
 * @id: Instance identifier.
 * @meta: Instance of #GstAnalyticsRelationMeta where the tag is stored.
 *
 * Handle for a semantic tag node. The tag is linked to its owner MTD with
 * GST_ANALYTICS_REL_TYPE_RELATE_TO.
 */
typedef struct _GstAnalyticsMtd GstAnalyticsSemanticTagMtd;

GST_ANALYTICS_META_API
GstAnalyticsMtdType gst_analytics_semantic_tag_mtd_get_mtd_type(void);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_add_semantic_tag_mtd(GstAnalyticsRelationMeta *relation_meta, const gchar *tag,
                                                          GstAnalyticsSemanticTagMtd *semantic_tag_mtd);

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_get_semantic_tag_mtd(GstAnalyticsRelationMeta *meta, guint id,
                                                          GstAnalyticsSemanticTagMtd *rlt);

GST_ANALYTICS_META_API
gboolean gst_analytics_semantic_tag_mtd_get_tag(const GstAnalyticsSemanticTagMtd *handle, const gchar **tag);

/* Fails if @mtd_id already has a semantic tag. Returned string must be freed with g_free(). */
GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_attach_semantic_tag(GstAnalyticsRelationMeta *meta, guint mtd_id,
                                                         const gchar *tag);

GST_ANALYTICS_META_API
gchar *gst_analytics_relation_meta_get_semantic_tag(GstAnalyticsRelationMeta *meta, guint mtd_id);

/*
 * Generic API with the same names and semantics as in patched GStreamer, built on semantic tag mtd.
 * The tag is stored as a GstAnalyticsSemanticTagMtd directly related (GST_ANALYTICS_REL_TYPE_RELATE_TO) to @instance.
 */

/* Replaces the tag of @instance, NULL or empty @tag removes it. */
GST_ANALYTICS_META_API
gboolean gst_analytics_mtd_set_semantic_tag(GstAnalyticsMtd *instance, const gchar *tag);

/* Returns a copy of the tag of @instance (empty string if not set), to be freed with g_free(). */
GST_ANALYTICS_META_API
gchar *gst_analytics_mtd_get_semantic_tag(const GstAnalyticsMtd *instance);

G_END_DECLS

#endif /* __GST_ANALYTICS_SEMANTIC_TAG_MTD_H__ */
