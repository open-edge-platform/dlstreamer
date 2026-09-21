/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "dlstreamer/gst/metadata/gva_dwelltime_meta.h"

#include <string.h>

typedef struct _GstAnalyticsDwellTimeData GstAnalyticsDwellTimeData;

struct _GstAnalyticsDwellTimeData {
    gdouble dwell_time;           /* seconds the object has been inside the zone */
    gdouble first_seen_timestamp; /* buffer PTS (seconds) when the object first entered the zone */
    gsize id_len;                 /* length of zone_id string including null terminator */
    gchar id[];                   /* flexible array member - must be last */
};

static const GstAnalyticsMtdImpl dwelltime_impl = {"dwelltime", NULL, NULL, {NULL}};

GstAnalyticsMtdType gst_analytics_dwelltime_mtd_get_mtd_type(void) {
    return (GstAnalyticsMtdType)&dwelltime_impl;
}

gboolean gst_analytics_dwelltime_mtd_get_info(const GstAnalyticsDwellTimeMtd *handle, gchar **zone_id,
                                              gdouble *dwell_time, gdouble *first_seen_timestamp) {
    g_return_val_if_fail(handle != NULL, FALSE);
    g_return_val_if_fail(handle->meta != NULL, FALSE);

    GstAnalyticsDwellTimeData *data =
        (GstAnalyticsDwellTimeData *)gst_analytics_relation_meta_get_mtd_data(handle->meta, handle->id);
    g_return_val_if_fail(data != NULL, FALSE);

    if (zone_id)
        *zone_id = g_strdup(data->id);
    if (dwell_time)
        *dwell_time = data->dwell_time;
    if (first_seen_timestamp)
        *first_seen_timestamp = data->first_seen_timestamp;

    return TRUE;
}

gboolean gst_analytics_relation_meta_add_dwelltime_mtd(GstAnalyticsRelationMeta *relation_meta, const gchar *zone_id,
                                                       gdouble dwell_time, gdouble first_seen_timestamp,
                                                       GstAnalyticsDwellTimeMtd *dwelltime_mtd) {
    g_return_val_if_fail(relation_meta != NULL, FALSE);
    g_return_val_if_fail(zone_id != NULL, FALSE);
    g_return_val_if_fail(dwelltime_mtd != NULL, FALSE);

    gsize id_len = strlen(zone_id) + 1;
    gsize size = sizeof(GstAnalyticsDwellTimeData) + id_len;

    GstAnalyticsDwellTimeData *data = (GstAnalyticsDwellTimeData *)gst_analytics_relation_meta_add_mtd(
        relation_meta, &dwelltime_impl, size, (GstAnalyticsMtd *)dwelltime_mtd);
    g_return_val_if_fail(data != NULL, FALSE);

    data->dwell_time = dwell_time;
    data->first_seen_timestamp = first_seen_timestamp;
    data->id_len = id_len;
    memcpy(data->id, zone_id, id_len);

    return TRUE;
}

gboolean gst_analytics_relation_meta_get_dwelltime_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                       GstAnalyticsDwellTimeMtd *rlt) {
    return gst_analytics_relation_meta_get_mtd(meta, an_meta_id, gst_analytics_dwelltime_mtd_get_mtd_type(),
                                               (GstAnalyticsDwellTimeMtd *)rlt);
}
