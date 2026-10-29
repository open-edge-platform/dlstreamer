/* GStreamer
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 * gstanalyticssemantictagmtd.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dlstreamer/gst/metadata/gstanalyticssemantictagmtd.h"

#include <string.h>

typedef struct _GstAnalyticsSemanticTagData GstAnalyticsSemanticTagData;

struct _GstAnalyticsSemanticTagData {
    gsize tag_len; /* length of tag including null terminator */
    gchar tag[];   /* flexible array member - must be last */
};

static const GstAnalyticsMtdImpl semantic_tag_impl = {"semantictag", NULL, NULL, {NULL}};

/**
 * gst_analytics_semantic_tag_mtd_get_mtd_type:
 *
 * Get an id identifying #GstAnalyticsSemanticTagMtd type.
 *
 * Returns: opaque id of #GstAnalyticsMtd type
 */
GstAnalyticsMtdType gst_analytics_semantic_tag_mtd_get_mtd_type(void) {
    return (GstAnalyticsMtdType)&semantic_tag_impl;
}

/**
 * gst_analytics_relation_meta_add_semantic_tag_mtd:
 * @relation_meta: Instance of #GstAnalyticsRelationMeta where to add the tag
 * @tag: Semantic tag, must not be empty
 * @semantic_tag_mtd: (out caller-allocates)(not nullable): Handle updated with the newly added tag
 *
 * Add a semantic tag node to @relation_meta. It is not related to any other metadata,
 * use gst_analytics_mtd_set_semantic_tag() to tag an existing metadata.
 *
 * Returns: TRUE if successful, FALSE otherwise.
 */
gboolean gst_analytics_relation_meta_add_semantic_tag_mtd(GstAnalyticsRelationMeta *relation_meta, const gchar *tag,
                                                          GstAnalyticsSemanticTagMtd *semantic_tag_mtd) {
    g_return_val_if_fail(relation_meta != NULL, FALSE);
    g_return_val_if_fail(tag != NULL && tag[0] != '\0', FALSE);
    g_return_val_if_fail(semantic_tag_mtd != NULL, FALSE);

    gsize tag_len = strlen(tag) + 1;
    gsize size = sizeof(GstAnalyticsSemanticTagData) + tag_len;

    GstAnalyticsSemanticTagData *data = (GstAnalyticsSemanticTagData *)gst_analytics_relation_meta_add_mtd(
        relation_meta, &semantic_tag_impl, size, (GstAnalyticsMtd *)semantic_tag_mtd);
    g_return_val_if_fail(data != NULL, FALSE);

    data->tag_len = tag_len;
    memcpy(data->tag, tag, tag_len);

    return TRUE;
}

/**
 * gst_analytics_relation_meta_get_semantic_tag_mtd:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @id: Id of the #GstAnalyticsSemanticTagMtd instance to retrieve
 * @rlt: (out caller-allocates)(not nullable): Will be filled with the semantic tag mtd
 *
 * Retrieve a semantic tag mtd from @meta by its id.
 *
 * Returns: TRUE if successful, FALSE otherwise.
 */
gboolean gst_analytics_relation_meta_get_semantic_tag_mtd(GstAnalyticsRelationMeta *meta, guint id,
                                                          GstAnalyticsSemanticTagMtd *rlt) {
    return gst_analytics_relation_meta_get_mtd(meta, id, gst_analytics_semantic_tag_mtd_get_mtd_type(),
                                               (GstAnalyticsMtd *)rlt);
}

/**
 * gst_analytics_semantic_tag_mtd_get_tag:
 * @handle: Instance of #GstAnalyticsSemanticTagMtd
 * @tag: (out)(transfer none)(not nullable): Tag string, owned by the relation meta
 *
 * Get the tag string stored in @handle.
 *
 * Returns: TRUE if successful, FALSE otherwise.
 */
gboolean gst_analytics_semantic_tag_mtd_get_tag(const GstAnalyticsSemanticTagMtd *handle, const gchar **tag) {
    g_return_val_if_fail(handle != NULL, FALSE);
    g_return_val_if_fail(handle->meta != NULL, FALSE);
    g_return_val_if_fail(tag != NULL, FALSE);

    GstAnalyticsSemanticTagData *data =
        (GstAnalyticsSemanticTagData *)gst_analytics_relation_meta_get_mtd_data(handle->meta, handle->id);
    g_return_val_if_fail(data != NULL, FALSE);

    *tag = data->tag;
    return TRUE;
}

/**
 * gst_analytics_relation_meta_attach_semantic_tag:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @mtd_id: Id of the metadata to tag
 * @tag: Semantic tag, must not be empty
 *
 * Add a semantic tag node and relate it directly to the metadata @mtd_id.
 * Fails if the metadata already has a semantic tag.
 *
 * Returns: TRUE if successful, FALSE otherwise.
 */
gboolean gst_analytics_relation_meta_attach_semantic_tag(GstAnalyticsRelationMeta *meta, guint mtd_id,
                                                         const gchar *tag) {
    g_return_val_if_fail(meta != NULL, FALSE);
    g_return_val_if_fail(tag != NULL && tag[0] != '\0', FALSE);

    gchar *existing = gst_analytics_relation_meta_get_semantic_tag(meta, mtd_id);
    if (existing) {
        g_free(existing);
        return FALSE;
    }

    GstAnalyticsSemanticTagMtd tag_mtd;
    if (!gst_analytics_relation_meta_add_semantic_tag_mtd(meta, tag, &tag_mtd))
        return FALSE;

    return gst_analytics_relation_meta_set_relation(meta, GST_ANALYTICS_REL_TYPE_RELATE_TO, mtd_id, tag_mtd.id);
}

/**
 * gst_analytics_relation_meta_get_semantic_tag:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @mtd_id: Id of the tagged metadata
 *
 * Get the semantic tag directly related to the metadata @mtd_id.
 *
 * Returns: (transfer full)(nullable): A copy of the tag, or %NULL if the metadata has no tag
 */
gchar *gst_analytics_relation_meta_get_semantic_tag(GstAnalyticsRelationMeta *meta, guint mtd_id) {
    g_return_val_if_fail(meta != NULL, NULL);

    gpointer state = NULL;
    GstAnalyticsMtd rlt;
    while (gst_analytics_relation_meta_get_direct_related(
        meta, mtd_id, GST_ANALYTICS_REL_TYPE_RELATE_TO, gst_analytics_semantic_tag_mtd_get_mtd_type(), &state, &rlt)) {
        GstAnalyticsSemanticTagData *data =
            (GstAnalyticsSemanticTagData *)gst_analytics_relation_meta_get_mtd_data(meta, rlt.id);
        if (data)
            return g_strdup(data->tag);
    }
    return NULL;
}

/**
 * gst_analytics_mtd_set_semantic_tag:
 * @instance: Instance of #GstAnalyticsMtd
 * @tag: (nullable): Semantic tag, %NULL or an empty string removes the tag
 *
 * Set the semantic tag of any analytics metadata, replacing the previous one. The tag is stored as
 * a #GstAnalyticsSemanticTagMtd directly related to @instance.
 *
 * Returns: TRUE if the tag was set successfully.
 */
gboolean gst_analytics_mtd_set_semantic_tag(GstAnalyticsMtd *instance, const gchar *tag) {
    g_return_val_if_fail(instance != NULL, FALSE);
    g_return_val_if_fail(instance->meta != NULL, FALSE);

    /* Drop the relation to the previous tag, its node stays in the meta unreferenced. */
    GstAnalyticsMtd previous;
    while (gst_analytics_relation_meta_get_direct_related(
        instance->meta, instance->id, GST_ANALYTICS_REL_TYPE_RELATE_TO, gst_analytics_semantic_tag_mtd_get_mtd_type(),
        NULL, &previous)) {
        if (!gst_analytics_relation_meta_set_relation(instance->meta, GST_ANALYTICS_REL_TYPE_NONE, instance->id,
                                                      previous.id))
            return FALSE;
    }

    if (tag == NULL || tag[0] == '\0')
        return TRUE;

    GstAnalyticsSemanticTagMtd tag_mtd;
    if (!gst_analytics_relation_meta_add_semantic_tag_mtd(instance->meta, tag, &tag_mtd))
        return FALSE;

    return gst_analytics_relation_meta_set_relation(instance->meta, GST_ANALYTICS_REL_TYPE_RELATE_TO, instance->id,
                                                    tag_mtd.id);
}

/**
 * gst_analytics_mtd_get_semantic_tag:
 * @instance: Instance of #GstAnalyticsMtd
 *
 * Get the semantic tag of any analytics metadata.
 *
 * Returns: (transfer full): A copy of the tag, an empty string if the metadata has no tag
 */
gchar *gst_analytics_mtd_get_semantic_tag(const GstAnalyticsMtd *instance) {
    g_return_val_if_fail(instance != NULL, NULL);
    g_return_val_if_fail(instance->meta != NULL, NULL);

    gchar *tag = gst_analytics_relation_meta_get_semantic_tag(instance->meta, instance->id);
    return tag ? tag : g_strdup("");
}
