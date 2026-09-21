/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "dlstreamer/gst/metadata/camera_3d_od_mtd.h"

GST_DEBUG_CATEGORY_EXTERN(gst_analytics_relation_meta_debug);
#define GST_CAT_DEFAULT gst_analytics_relation_meta_debug

/**
 * SECTION:gstanalyticscamera3dobjectdetectionmtd
 * @title: GstAnalyticsCamera3DODMtd
 * @short_description: Analytics metadata for monocular 3D object detection inside a #GstAnalyticsRelationMeta
 * @symbols:
 * - GstAnalyticsCamera3DODMtd
 * @see_also: #GstAnalyticsMtd, #GstAnalyticsRelationMeta, #GstAnalyticsODMtd, #GstAnalytics3DODMtd
 *
 * This metadata type stores a monocular (camera-frame) 3D detection: the amodal
 * 2D box, the 3D location (bottom-centre, rectified camera frame), the object
 * dimensions, the heading (rotation_y) and the observation angle (alpha), along
 * with the detected class and confidence. Unlike #GstAnalytics3DODMtd (a
 * world-frame LiDAR/radar box with yaw/pitch/roll), this type is expressed in
 * the KITTI rectified camera frame (x-right, y-down, z-forward) with a single
 * rotation about the camera Y axis, matching monocular 3D detectors.
 */

typedef struct _GstAnalyticsCamera3DODMtdData GstAnalyticsCamera3DODMtdData;

struct _GstAnalyticsCamera3DODMtdData {
    gint class_id;
    gfloat confidence;
    /* Amodal 2D box in pixels of the original image. */
    gfloat x1;
    gfloat y1;
    gfloat x2;
    gfloat y2;
    /* 3D location, rectified camera frame, bottom-centre, in metres. */
    gfloat x;
    gfloat y;
    gfloat z;
    /* Object dimensions, in metres. */
    gfloat height;
    gfloat width;
    gfloat length;
    /* Heading around camera Y axis and observation angle, in radians. */
    gfloat rotation_y;
    gfloat alpha;
};

static gboolean gst_analytics_camera_3d_od_mtd_meta_transform(GstBuffer *transbuf, GstAnalyticsMtd *transmtd,
                                                              GstBuffer *buffer, GQuark type, gpointer data) {
    (void)transbuf;
    (void)transmtd;
    (void)buffer;
    (void)type;
    (void)data;
    /* The 2D box is in pixel space and the 3D box is projected with the camera
     * intrinsics of the original frame; 2D scaling/cropping between detection
     * and rendering is not tracked here. Copy the meta as-is. */
    return TRUE;
}

static const GstAnalyticsMtdImpl _camera_3d_od_impl = {
    "camera-3d-object-detection", gst_analytics_camera_3d_od_mtd_meta_transform, NULL, {NULL}};

/**
 * gst_analytics_camera_3d_od_mtd_get_mtd_type:
 *
 * Get an id that represents the camera-3d-object-detection metadata type.
 *
 * Returns: opaque id of the #GstAnalyticsMtd type
 */
GstAnalyticsMtdType gst_analytics_camera_3d_od_mtd_get_mtd_type(void) {
    return (GstAnalyticsMtdType)&_camera_3d_od_impl;
}

/**
 * gst_analytics_relation_meta_add_camera_3d_od_mtd:
 * @instance: Instance of #GstAnalyticsRelationMeta where to add the detection
 * @class_id: Detected class index (negative if unknown)
 * @confidence: Detection confidence
 * @x1: Left of the amodal 2D box, in pixels
 * @y1: Top of the amodal 2D box, in pixels
 * @x2: Right of the amodal 2D box, in pixels
 * @y2: Bottom of the amodal 2D box, in pixels
 * @x: X of the 3D location (rectified camera frame, bottom-centre), in metres
 * @y: Y of the 3D location (rectified camera frame, bottom-centre), in metres
 * @z: Z of the 3D location (rectified camera frame, bottom-centre), in metres
 * @height: Object height, in metres
 * @width: Object width, in metres
 * @length: Object length, in metres
 * @rotation_y: Heading around the camera Y axis, in radians
 * @alpha: Observation angle, in radians
 * @mtd: (out) (not nullable): Handle updated with the newly added meta
 *
 * Returns: TRUE on success, FALSE otherwise
 */
gboolean gst_analytics_relation_meta_add_camera_3d_od_mtd(GstAnalyticsRelationMeta *instance, gint class_id,
                                                          gfloat confidence, gfloat x1, gfloat y1, gfloat x2, gfloat y2,
                                                          gfloat x, gfloat y, gfloat z, gfloat height, gfloat width,
                                                          gfloat length, gfloat rotation_y, gfloat alpha,
                                                          GstAnalyticsCamera3DODMtd *mtd) {
    g_return_val_if_fail(instance != NULL, FALSE);
    g_return_val_if_fail(mtd != NULL, FALSE);

    GstAnalyticsCamera3DODMtdData *mtd_data = (GstAnalyticsCamera3DODMtdData *)gst_analytics_relation_meta_add_mtd(
        instance, &_camera_3d_od_impl, sizeof(GstAnalyticsCamera3DODMtdData), mtd);

    if (!mtd_data)
        return FALSE;

    mtd_data->class_id = class_id;
    mtd_data->confidence = confidence;
    mtd_data->x1 = x1;
    mtd_data->y1 = y1;
    mtd_data->x2 = x2;
    mtd_data->y2 = y2;
    mtd_data->x = x;
    mtd_data->y = y;
    mtd_data->z = z;
    mtd_data->height = height;
    mtd_data->width = width;
    mtd_data->length = length;
    mtd_data->rotation_y = rotation_y;
    mtd_data->alpha = alpha;
    return TRUE;
}

/**
 * gst_analytics_camera_3d_od_mtd_get_class:
 * @instance: instance
 * @class_id: (out): detected class id
 * @confidence: (out): confidence
 *
 * Returns: TRUE on success, FALSE otherwise
 */
gboolean gst_analytics_camera_3d_od_mtd_get_class(const GstAnalyticsCamera3DODMtd *instance, gint *class_id,
                                                  gfloat *confidence) {
    GstAnalyticsCamera3DODMtdData *data;

    g_return_val_if_fail(instance && class_id && confidence, FALSE);
    data = gst_analytics_relation_meta_get_mtd_data(instance->meta, instance->id);
    g_return_val_if_fail(data != NULL, FALSE);

    *class_id = data->class_id;
    *confidence = data->confidence;
    return TRUE;
}

/**
 * gst_analytics_camera_3d_od_mtd_get_box2d:
 * @instance: instance
 * @x1: (out): left of the amodal 2D box
 * @y1: (out): top of the amodal 2D box
 * @x2: (out): right of the amodal 2D box
 * @y2: (out): bottom of the amodal 2D box
 *
 * Retrieve the amodal 2D box, in pixels of the original image.
 *
 * Returns: TRUE on success, FALSE otherwise
 */
gboolean gst_analytics_camera_3d_od_mtd_get_box2d(const GstAnalyticsCamera3DODMtd *instance, gfloat *x1, gfloat *y1,
                                                  gfloat *x2, gfloat *y2) {
    GstAnalyticsCamera3DODMtdData *data;

    g_return_val_if_fail(instance && x1 && y1 && x2 && y2, FALSE);
    data = gst_analytics_relation_meta_get_mtd_data(instance->meta, instance->id);
    g_return_val_if_fail(data != NULL, FALSE);

    *x1 = data->x1;
    *y1 = data->y1;
    *x2 = data->x2;
    *y2 = data->y2;
    return TRUE;
}

/**
 * gst_analytics_camera_3d_od_mtd_get_location:
 * @instance: instance
 * @x: (out): X of the 3D location (rectified camera frame, bottom-centre)
 * @y: (out): Y of the 3D location (rectified camera frame, bottom-centre)
 * @z: (out): Z of the 3D location (rectified camera frame, bottom-centre)
 * @height: (out): object height
 * @width: (out): object width
 * @length: (out): object length
 * @rotation_y: (out): heading around the camera Y axis
 * @alpha: (out): observation angle
 *
 * Retrieve the 3D box in the rectified camera frame.
 *
 * Returns: TRUE on success, FALSE otherwise
 */
gboolean gst_analytics_camera_3d_od_mtd_get_location(const GstAnalyticsCamera3DODMtd *instance, gfloat *x, gfloat *y,
                                                     gfloat *z, gfloat *height, gfloat *width, gfloat *length,
                                                     gfloat *rotation_y, gfloat *alpha) {
    GstAnalyticsCamera3DODMtdData *data;

    g_return_val_if_fail(instance && x && y && z && height && width && length && rotation_y && alpha, FALSE);
    data = gst_analytics_relation_meta_get_mtd_data(instance->meta, instance->id);
    g_return_val_if_fail(data != NULL, FALSE);

    *x = data->x;
    *y = data->y;
    *z = data->z;
    *height = data->height;
    *width = data->width;
    *length = data->length;
    *rotation_y = data->rotation_y;
    *alpha = data->alpha;
    return TRUE;
}

/**
 * gst_analytics_relation_meta_get_camera_3d_od_mtd:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @an_meta_id: Id of #GstAnalyticsCamera3DODMtd instance to retrieve
 * @rlt: (out caller-allocates) (not nullable): Will be filled with the detection mtd
 *
 * Returns: TRUE if successful, FALSE otherwise
 */
gboolean gst_analytics_relation_meta_get_camera_3d_od_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                          GstAnalyticsCamera3DODMtd *rlt) {
    return gst_analytics_relation_meta_get_mtd(meta, an_meta_id, gst_analytics_camera_3d_od_mtd_get_mtd_type(),
                                               (GstAnalyticsCamera3DODMtd *)rlt);
}
