/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "dlstreamer/gst/metadata/gva_zone_meta.h"

#include <string.h>

typedef struct _GstAnalyticsZoneData GstAnalyticsZoneData;

struct _GstAnalyticsZoneData {
    gsize id_len; /* length of id string including null terminator */
    gchar id[];   /* flexible array member - must be last */
};

static const GstAnalyticsMtdImpl zone_impl = {"zone", NULL, NULL, {NULL}};

GstAnalyticsMtdType gst_analytics_zone_mtd_get_mtd_type(void) {
    return (GstAnalyticsMtdType)&zone_impl;
}

gboolean gst_analytics_zone_mtd_get_info(const GstAnalyticsZoneMtd *handle, gchar **zone_id) {
    g_return_val_if_fail(handle != NULL, FALSE);
    g_return_val_if_fail(handle->meta != NULL, FALSE);

    GstAnalyticsZoneData *data =
        (GstAnalyticsZoneData *)gst_analytics_relation_meta_get_mtd_data(handle->meta, handle->id);
    g_return_val_if_fail(data != NULL, FALSE);

    if (zone_id)
        *zone_id = g_strdup(data->id);

    return TRUE;
}

gboolean gst_analytics_relation_meta_add_zone_mtd(GstAnalyticsRelationMeta *relation_meta, const gchar *zone_id,
                                                  GstAnalyticsZoneMtd *zone_mtd) {
    g_return_val_if_fail(relation_meta != NULL, FALSE);
    g_return_val_if_fail(zone_id != NULL, FALSE);
    g_return_val_if_fail(zone_mtd != NULL, FALSE);

    gsize id_len = strlen(zone_id) + 1;
    gsize size = sizeof(GstAnalyticsZoneData) + id_len;

    GstAnalyticsZoneData *data = (GstAnalyticsZoneData *)gst_analytics_relation_meta_add_mtd(
        relation_meta, &zone_impl, size, (GstAnalyticsMtd *)zone_mtd);
    g_return_val_if_fail(data != NULL, FALSE);

    data->id_len = id_len;
    memcpy(data->id, zone_id, id_len);

    return TRUE;
}
