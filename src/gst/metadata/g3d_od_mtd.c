/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "dlstreamer/gst/metadata/g3d_od_mtd.h"

GST_DEBUG_CATEGORY_EXTERN(gst_analytics_relation_meta_debug);
#define GST_CAT_DEFAULT gst_analytics_relation_meta_debug

/**
 * SECTION:gstanalytics3dobjectdetectionmtd
 * @title: GstAnalytics3DODMtd
 * @short_description: Analytics metadata for 3D object detection inside a #GstAnalyticsRelationMeta
 * @symbols:
 * - GstAnalytics3DODMtd
 * - GstAnalytics3DSensorModality
 * @see_also: #GstAnalyticsMtd, #GstAnalyticsRelationMeta, #GstAnalyticsODMtd, #GstAnalyticsTrackingMtd
 *
 * This metadata type stores a 3D oriented bounding box (x, y, z, length, width,
 * height, yaw, pitch, roll) along with the detected class, confidence, and the
 * sensor modality the detection was sourced from.
 */

typedef struct _GstAnalytics3DODMtdData GstAnalytics3DODMtdData;

struct _GstAnalytics3DODMtdData {
    gfloat x;
    gfloat y;
    gfloat z;
    gfloat length;
    gfloat width;
    gfloat height;
    gfloat yaw;
    gfloat pitch;
    gfloat roll;
    gint class_id;
    gfloat confidence;
    GstAnalytics3DSensorModality modality;
};

static gboolean gst_analytics_3d_od_mtd_meta_transform(GstBuffer *transbuf, GstAnalyticsMtd *transmtd,
                                                       GstBuffer *buffer, GQuark type, gpointer data) {
    (void)transbuf;
    (void)transmtd;
    (void)buffer;
    (void)type;
    (void)data;
    /* 3D detections are in a sensor/world frame; 2D video transforms (scale,
     * matrix) do not affect them. Allow the meta to be copied as-is. */
    return TRUE;
}

static const GstAnalyticsMtdImpl _3d_od_impl = {
    "3d-object-detection", gst_analytics_3d_od_mtd_meta_transform, NULL, {NULL}};

/**
 * gst_analytics_3d_od_mtd_get_mtd_type:
 *
 * Get an id that represents the 3d-object-detection metadata type.
 *
 * Returns: opaque id of the #GstAnalyticsMtd type
 */
GstAnalyticsMtdType gst_analytics_3d_od_mtd_get_mtd_type(void) {
    return (GstAnalyticsMtdType)&_3d_od_impl;
}

/**
 * gst_analytics_relation_meta_add_3d_od_mtd:
 * @instance: Instance of #GstAnalyticsRelationMeta where to add the 3D detection
 * @x: X coordinate of the box centre, in metres
 * @y: Y coordinate of the box centre, in metres
 * @z: Z coordinate of the box centre, in metres
 * @length: Box extent along X, in metres
 * @width: Box extent along Y, in metres
 * @height: Box extent along Z, in metres
 * @yaw: Yaw rotation around Z, in radians
 * @pitch: Pitch rotation around Y, in radians
 * @roll: Roll rotation around X, in radians
 * @class_id: Detected class index (negative if unknown)
 * @confidence: Detection confidence in [0, 1]
 * @modality: Sensor modality this detection originates from
 * @mtd: (out) (not nullable): Handle updated with the newly added meta
 *
 * Returns: TRUE on success, FALSE otherwise
 */
gboolean gst_analytics_relation_meta_add_3d_od_mtd(GstAnalyticsRelationMeta *instance, gfloat x, gfloat y, gfloat z,
                                                   gfloat length, gfloat width, gfloat height, gfloat yaw, gfloat pitch,
                                                   gfloat roll, gint class_id, gfloat confidence,
                                                   GstAnalytics3DSensorModality modality, GstAnalytics3DODMtd *mtd) {
    g_return_val_if_fail(instance != NULL, FALSE);
    g_return_val_if_fail(mtd != NULL, FALSE);

    GstAnalytics3DODMtdData *mtd_data = (GstAnalytics3DODMtdData *)gst_analytics_relation_meta_add_mtd(
        instance, &_3d_od_impl, sizeof(GstAnalytics3DODMtdData), mtd);

    if (!mtd_data)
        return FALSE;

    mtd_data->x = x;
    mtd_data->y = y;
    mtd_data->z = z;
    mtd_data->length = length;
    mtd_data->width = width;
    mtd_data->height = height;
    mtd_data->yaw = yaw;
    mtd_data->pitch = pitch;
    mtd_data->roll = roll;
    mtd_data->class_id = class_id;
    mtd_data->confidence = confidence;
    mtd_data->modality = modality;
    return TRUE;
}

/**
 * gst_analytics_3d_od_mtd_get_location:
 * @instance: instance
 * @x: (out): X coordinate of the box centre
 * @y: (out): Y coordinate of the box centre
 * @z: (out): Z coordinate of the box centre
 * @length: (out): Length of the box
 * @width: (out): Width of the box
 * @height: (out): Height of the box
 * @yaw: (out): Yaw rotation around Z
 * @pitch: (out): Pitch rotation around Y
 * @roll: (out): Roll rotation around X
 *
 * Retrieve the 3D oriented box.
 *
 * Returns: TRUE on success, FALSE otherwise
 */
gboolean gst_analytics_3d_od_mtd_get_location(const GstAnalytics3DODMtd *instance, gfloat *x, gfloat *y, gfloat *z,
                                              gfloat *length, gfloat *width, gfloat *height, gfloat *yaw, gfloat *pitch,
                                              gfloat *roll) {
    GstAnalytics3DODMtdData *data;

    g_return_val_if_fail(instance && x && y && z && length && width && height && yaw && pitch && roll, FALSE);
    data = gst_analytics_relation_meta_get_mtd_data(instance->meta, instance->id);
    g_return_val_if_fail(data != NULL, FALSE);

    *x = data->x;
    *y = data->y;
    *z = data->z;
    *length = data->length;
    *width = data->width;
    *height = data->height;
    *yaw = data->yaw;
    *pitch = data->pitch;
    *roll = data->roll;
    return TRUE;
}

/**
 * gst_analytics_3d_od_mtd_get_class:
 * @instance: instance
 * @class_id: (out): detected class id
 * @confidence: (out): confidence in [0, 1]
 *
 * Returns: TRUE on success, FALSE otherwise
 */
gboolean gst_analytics_3d_od_mtd_get_class(const GstAnalytics3DODMtd *instance, gint *class_id, gfloat *confidence) {
    GstAnalytics3DODMtdData *data;

    g_return_val_if_fail(instance && class_id && confidence, FALSE);
    data = gst_analytics_relation_meta_get_mtd_data(instance->meta, instance->id);
    g_return_val_if_fail(data != NULL, FALSE);

    *class_id = data->class_id;
    *confidence = data->confidence;
    return TRUE;
}

/**
 * gst_analytics_3d_od_mtd_get_modality:
 * @instance: instance
 * @modality: (out): sensor modality
 *
 * Returns: TRUE on success, FALSE otherwise
 */
gboolean gst_analytics_3d_od_mtd_get_modality(const GstAnalytics3DODMtd *instance,
                                              GstAnalytics3DSensorModality *modality) {
    GstAnalytics3DODMtdData *data;

    g_return_val_if_fail(instance && modality, FALSE);
    data = gst_analytics_relation_meta_get_mtd_data(instance->meta, instance->id);
    g_return_val_if_fail(data != NULL, FALSE);

    *modality = data->modality;
    return TRUE;
}

/**
 * gst_analytics_relation_meta_get_3d_od_mtd:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @an_meta_id: Id of #GstAnalytics3DODMtd instance to retrieve
 * @rlt: (out caller-allocates) (not nullable): Will be filled with the 3D detection mtd
 *
 * Returns: TRUE if successful, FALSE otherwise
 */
gboolean gst_analytics_relation_meta_get_3d_od_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                   GstAnalytics3DODMtd *rlt) {
    return gst_analytics_relation_meta_get_mtd(meta, an_meta_id, gst_analytics_3d_od_mtd_get_mtd_type(),
                                               (GstAnalytics3DODMtd *)rlt);
}
