/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "dlstreamer/gst/metadata/gva_tripwire_meta.h"

#include <string.h>

typedef struct _GstAnalyticsTripwireData GstAnalyticsTripwireData;

struct _GstAnalyticsTripwireData {
    gint direction;
    gsize id_len; /* length of id string including null terminator */
    gchar id[];   /* flexible array member - must be last */
};

static const GstAnalyticsMtdImpl tripwire_impl = {"tripwire", NULL, NULL, {NULL}};

GstAnalyticsMtdType gst_analytics_tripwire_mtd_get_mtd_type(void) {
    return (GstAnalyticsMtdType)&tripwire_impl;
}

gboolean gst_analytics_tripwire_mtd_get_info(const GstAnalyticsTripwireMtd *handle, gchar **tripwire_id,
                                             gint *direction) {
    g_return_val_if_fail(handle != NULL, FALSE);
    g_return_val_if_fail(handle->meta != NULL, FALSE);

    GstAnalyticsTripwireData *data =
        (GstAnalyticsTripwireData *)gst_analytics_relation_meta_get_mtd_data(handle->meta, handle->id);
    g_return_val_if_fail(data != NULL, FALSE);

    if (tripwire_id)
        *tripwire_id = g_strdup(data->id);

    if (direction)
        *direction = data->direction;

    return TRUE;
}

gboolean gst_analytics_relation_meta_add_tripwire_mtd(GstAnalyticsRelationMeta *relation_meta, const gchar *tripwire_id,
                                                      gint direction, GstAnalyticsTripwireMtd *tripwire_mtd) {
    g_return_val_if_fail(relation_meta != NULL, FALSE);
    g_return_val_if_fail(tripwire_id != NULL, FALSE);
    g_return_val_if_fail(tripwire_mtd != NULL, FALSE);

    gsize id_len = strlen(tripwire_id) + 1;
    gsize size = sizeof(GstAnalyticsTripwireData) + id_len;

    GstAnalyticsTripwireData *data = (GstAnalyticsTripwireData *)gst_analytics_relation_meta_add_mtd(
        relation_meta, &tripwire_impl, size, (GstAnalyticsMtd *)tripwire_mtd);
    g_return_val_if_fail(data != NULL, FALSE);

    data->direction = direction;
    data->id_len = id_len;
    memcpy(data->id, tripwire_id, id_len);

    return TRUE;
}

gboolean gst_analytics_relation_meta_get_tripwire_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                      GstAnalyticsTripwireMtd *rlt) {
    return gst_analytics_relation_meta_get_mtd(meta, an_meta_id, gst_analytics_tripwire_mtd_get_mtd_type(),
                                               (GstAnalyticsTripwireMtd *)rlt);
}
