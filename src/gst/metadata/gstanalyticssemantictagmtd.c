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

GstAnalyticsMtdType gst_analytics_semantic_tag_mtd_get_mtd_type(void) {
    return (GstAnalyticsMtdType)&semantic_tag_impl;
}

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

gboolean gst_analytics_relation_meta_get_semantic_tag_mtd(GstAnalyticsRelationMeta *meta, guint id,
                                                          GstAnalyticsSemanticTagMtd *rlt) {
    return gst_analytics_relation_meta_get_mtd(meta, id, gst_analytics_semantic_tag_mtd_get_mtd_type(),
                                               (GstAnalyticsMtd *)rlt);
}

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

gchar *gst_analytics_mtd_get_semantic_tag(const GstAnalyticsMtd *instance) {
    g_return_val_if_fail(instance != NULL, NULL);
    g_return_val_if_fail(instance->meta != NULL, NULL);

    gchar *tag = gst_analytics_relation_meta_get_semantic_tag(instance->meta, instance->id);
    return tag ? tag : g_strdup("");
}
