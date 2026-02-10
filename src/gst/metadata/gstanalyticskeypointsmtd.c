/*******************************************************************************
 * Copyright (C) 2024-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dlstreamer/gst/metadata/gstanalyticskeypointsmtd.h"

#include <gst/video/video.h>

/**
 * SECTION:gstanalyticsgeypointmtd
 * @title: GstAnalyticsKeypointMtd
 * @short_description: An analytics metadata describing a keypoint inside a #GstAnalyticsKeypointGroup
 * @symbols:
 * - GstAnalyticsKeypointMtd
 * @see_also: #GstAnalyticsMtd, #GstAnalyticsRelationMeta
 *
 * This type of metadata holds attributes of a keypoint
 *
 * Since: 1.26
 */

static gboolean gst_analytics_keypoint_mtd_meta_transform(GstBuffer *transbuf, GstAnalyticsMtd *transmtd,
                                                          GstBuffer *buffer, GQuark type, gpointer data) {
    (void)transbuf;
    (void)buffer;

    if (GST_VIDEO_META_TRANSFORM_IS_SCALE(type)) {
        GstVideoMetaTransform *trans = data;
        gint ow, oh, nw, nh;
        GstAnalyticsKeypoint *keypoint;

        ow = GST_VIDEO_INFO_WIDTH(trans->in_info);
        nw = GST_VIDEO_INFO_WIDTH(trans->out_info);
        oh = GST_VIDEO_INFO_HEIGHT(trans->in_info);
        nh = GST_VIDEO_INFO_HEIGHT(trans->out_info);

        keypoint = gst_analytics_relation_meta_get_mtd_data(transmtd->meta, transmtd->id);

        keypoint->x = keypoint->x * nw / ow;
        keypoint->y = keypoint->y * nh / oh;
    }

    return TRUE;
}

static const GstAnalyticsMtdImpl keypoint_impl = {"keypoint", gst_analytics_keypoint_mtd_meta_transform, NULL, {NULL}};

/**
 * gst_analytics_keypoint_mtd_get_mtd_type:
 *
 * Get an id that represents keypoint metadata type
 *
 * Returns: Opaque id of the #GstAnalyticsMtd type
 *
 * Since: 1.26
 */
GST_ANALYTICS_META_API
GstAnalyticsMtdType gst_analytics_keypoint_mtd_get_mtd_type(void) {
    return (GstAnalyticsMtdType)&keypoint_impl;
}

/**
 * gst_analytics_keypoint_mtd_get:
 * @handle: instance
 * @keypoint: (out caller-allocates): data structure describing keypoint attributes
 *
 * Retrieve keypoint attributes.
 *
 * Returns: TRUE on success, otherwise FALSE.
 *
 * Since: 1.26
 */
GST_ANALYTICS_META_API
gboolean gst_analytics_keypoint_mtd_get(const GstAnalyticsKeypointMtd *handle, GstAnalyticsKeypoint *keypoint) {
    g_return_val_if_fail(handle, FALSE);
    g_return_val_if_fail(handle->meta != NULL, FALSE);

    GstAnalyticsKeypoint *keypoint_data =
        (GstAnalyticsKeypoint *)gst_analytics_relation_meta_get_mtd_data(handle->meta, handle->id);
    g_return_val_if_fail(keypoint_data != NULL, FALSE);

    memcpy(keypoint, keypoint_data, sizeof(GstAnalyticsKeypoint));

    return TRUE;
}

/**
 * gst_analytics_relation_meta_add_keypoint_mtd:
 * @instance: Instance of #GstAnalyticsRelationMeta where to add keypoint metadata.
 * @keypoint: keypoint attributes to store as metadata.
 * @keypoint_mtd: (out caller-allocates) (not nullable): Handle updated to newly added keypoint meta.
 *
 * Add analytic keypoint metadata to @instance.
 *
 * Returns: TRUE on success, otherwise FALSE.
 *
 * Since: 1.26
 */
GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_add_keypoint_mtd(GstAnalyticsRelationMeta *instance,
                                                      const GstAnalyticsKeypoint *keypoint,
                                                      GstAnalyticsKeypointMtd *keypoint_mtd) {
    g_return_val_if_fail(instance, FALSE);
    g_return_val_if_fail(keypoint != NULL, FALSE);

    gsize size = sizeof(GstAnalyticsKeypoint);
    GstAnalyticsKeypoint *keypoint_data = (GstAnalyticsKeypoint *)gst_analytics_relation_meta_add_mtd(
        instance, &keypoint_impl, size, (GstAnalyticsMtd *)keypoint_mtd);
    g_return_val_if_fail(keypoint_data != NULL, FALSE);

    memcpy(keypoint_data, keypoint, sizeof(GstAnalyticsKeypoint));

    return TRUE;
}

/**
 * gst_analytics_relation_meta_get_keypoint_mtd:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @an_meta_id: Id of #GstAnalyticsKeypointMtd instance to retrieve
 * @rlt: (out caller-allocates)(not nullable): Will be filled with relatable meta
 *
 * Fill @rlt if a analytics-meta with id == @an_meta_id exist in @meta instance,
 * otherwise this method return FALSE and @rlt is invalid.
 *
 * Returns: TRUE if successful.
 *
 * Since: 1.26
 */
GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_get_keypoint_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                      GstAnalyticsKeypointMtd *rlt) {
    return gst_analytics_relation_meta_get_mtd(meta, an_meta_id, gst_analytics_keypoint_mtd_get_mtd_type(),
                                               (GstAnalyticsMtd *)rlt);
}

/**
 * SECTION:gstanalyticskeypointskeletonmtd
 * @title: GstAnalyticsKeypointSkeletonMtd
 * @short_description: An analytics metadata for keypoint skeleton defintion of #GstAnalyticsRelationMeta
 * @symbols:
 * - GstAnalyticsKeyponitSkeletonMtd
 * @see_also: #GstAnalyticsKeypointMtd, #GstAnalyticsMtd, #GstAnalyticsRelationMeta
 *
 * This type of metadata holds skeleton relation between keypoints,
 * it is used in relationship with keypoints group metadata.
 *
 * Since: 1.26
 */

static const GstAnalyticsMtdImpl keypoint_skeleton_impl = {"keypoint_skeleton", NULL, NULL, {NULL}};

typedef struct _GstAnalyticsKeypointSkeletonData GstAnalyticsKeypointSkeletonData;

struct _GstAnalyticsKeypointSkeletonData {
    gsize count;
    GstAnalyticsKeypointPair segments[]; /* must be last */
};

/**
 * gst_analytics_keypoint_skeleton_mtd_get_mtd_type:
 *
 * Get an id identifying #GstAnalyticsKeypointSkeletonMtd type.
 *
 * Returns: opaque id of #GstAnalyticsKeypointSkeletonMtd type
 *
 * Since: 1.26
 */
GstAnalyticsMtdType gst_analytics_keypoint_skeleton_mtd_get_mtd_type(void) {
    return (GstAnalyticsMtdType)&keypoint_skeleton_impl;
}

/**
 * gst_analytics_keypoint_skeleton_mtd_get_count:
 * @handle: instance handle
 *
 * Get number of skeleton segments
 * Returns: number of skeleton segments in this instance
 *
 * Since: 1.26
 */
gsize gst_analytics_keypoint_skeleton_mtd_get_count(const GstAnalyticsKeypointSkeletonMtd *handle) {
    g_return_val_if_fail(handle, 0);
    g_return_val_if_fail(handle->meta != NULL, 0);

    GstAnalyticsKeypointSkeletonData *data =
        (GstAnalyticsKeypointSkeletonData *)gst_analytics_relation_meta_get_mtd_data(handle->meta, handle->id);
    g_return_val_if_fail(data != NULL, 0);

    return data->count;
}

/**
 * gst_analytics_keypoint_skeleton_mtd_get:
 * @handle: instance handle
 * @segment: (out caller-allocates): a pair of keypoint indices representing a skeleton segment
 * @index: skeleton segment index, must be < gst_analytics_keypoint_skeleton_mtd_get_count
 *
 * Get pair of keypoint indices for a skeleton link at @index
 * Returns: TRUE if valid skeleton segment returned for @index, FALSE if the call failed
 *
 * Since: 1.26
 */
gboolean gst_analytics_keypoint_skeleton_mtd_get(const GstAnalyticsKeypointSkeletonMtd *handle,
                                                 GstAnalyticsKeypointPair *segment, gsize index) {
    g_return_val_if_fail(handle, FALSE);
    g_return_val_if_fail(handle->meta != NULL, FALSE);

    GstAnalyticsKeypointSkeletonData *data =
        (GstAnalyticsKeypointSkeletonData *)gst_analytics_relation_meta_get_mtd_data(handle->meta, handle->id);
    g_return_val_if_fail(data != NULL, FALSE);
    g_return_val_if_fail(data->count > index, FALSE);

    memcpy(segment, &data->segments[index], sizeof(GstAnalyticsKeypointPair));

    return TRUE;
}

/**
 * gst_analytics_relation_meta_add_keypoint_skeleton_mtd:
 * @instance: Instance of #GstAnalyticsRelationMeta where to add classification instance
 * @skeleton_count: number of skeleton segments that connect keypoints
 * @skeletons: (array length=skeleton_count): keypoint pair indices representing skeleton segments
 * example: < {0, 2}, {0, 5}, ...>
 * @keypoint_skeleton_mtd: (out caller-allocates) (not nullable): Handle updated to newly added keypoint info.
 *
 * Add analytic keypoint metadata to @instance.
 * Returns: Added successfully
 *
 * Since: 1.26
 */
gboolean gst_analytics_relation_meta_add_keypoint_skeleton_mtd(GstAnalyticsRelationMeta *instance,
                                                               const gsize skeleton_count,
                                                               const GstAnalyticsKeypointPair *skeletons,
                                                               GstAnalyticsKeypointSkeletonMtd *keypoint_skeleton_mtd) {
    g_return_val_if_fail(instance, FALSE);
    g_return_val_if_fail(skeletons != NULL, FALSE);

    gsize data_size = sizeof(GstAnalyticsKeypointPair) * skeleton_count;
    gsize size = sizeof(GstAnalyticsKeypointSkeletonData) + data_size;

    GstAnalyticsKeypointSkeletonData *data = (GstAnalyticsKeypointSkeletonData *)gst_analytics_relation_meta_add_mtd(
        instance, &keypoint_skeleton_impl, size, (GstAnalyticsMtd *)keypoint_skeleton_mtd);

    g_return_val_if_fail(data != NULL, FALSE);

    // fill in skeleton segments array
    data->count = skeleton_count;
    memcpy(data->segments, skeletons, skeleton_count * sizeof(GstAnalyticsKeypointPair));

    return TRUE;
}

/**
 * gst_analytics_relation_meta_get_keypoint_skeleton_mtd:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @an_meta_id: Id of #GstAnalyticsKeypointSkeletonMtd instance to retrieve
 * @rlt: (out caller-allocates)(not nullable): Will be filled with relatable meta
 *
 * Fill @rlt if a analytics-meta with id == @an_meta_id exist in @meta instance,
 * otherwise this method return FALSE and @rlt is invalid.
 *
 * Returns: TRUE if successful.
 *
 * Since: 1.26
 */
GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_get_keypoint_skeleton_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                               GstAnalyticsKeypointSkeletonMtd *rlt) {
    return gst_analytics_relation_meta_get_mtd(meta, an_meta_id, gst_analytics_keypoint_skeleton_mtd_get_mtd_type(),
                                               (GstAnalyticsMtd *)rlt);
}

/**
 * SECTION:gstanalyticskeypointgroupmtd
 * @title: GstAnalyticsKeypointGroupMtd
 * @short_description: An analytics metadata for an ordered group of keypoints inside a #GstAnalyticsRelationMeta
 * @symbols:
 * - GstAnalyticsKeypointGroupMtd
 * @see_also: #GstAnalyticsMtd, #GstAnalyticsRelationMeta
 *
 * This type of metadata holds an ordered list of keypoints that compose a keypoint group.
 * This metadata is typilcally used in relationship with another metadata to enhance it content.
 *
 * The method gst_analytics_relation_meta_set_keypointgroup_relations() sets default
 * relationsbetween keypoints, keypoint group, keypoint names and skeleton.
 *
 * Since: 1.26
 */

typedef struct _GstAnalyticsKeypointGroupData GstAnalyticsKeypointGroupData;

struct _GstAnalyticsKeypointGroupData {
    gsize count;
    guint ids[]; /* must be last */
};

static const GstAnalyticsMtdImpl keypoint_group_impl = {"keypoint_group", NULL, NULL, {NULL}};

/**
 * gst_analytics_keypointgroup_mtd_get_mtd_type:
 *
 * Get an id identifying #GstAnalyticsKeypointGropuMtd type.
 *
 * Returns: opaque id of #GstAnalyticsMtd type
 *
 * Since: 1.26
 */
GstAnalyticsMtdType gst_analytics_keypointgroup_mtd_get_mtd_type(void) {
    return (GstAnalyticsMtdType)&keypoint_group_impl;
}

/**
 * gst_analytics_keypoint_group_mtd_get_mtd_type:
 *
 * Get an id identifying #GstAnalyticsKeypointGroupMtd type.
 * For GIR bindings compatibility. Ideally should rename all
 * keypointgroup to keypoint_group.
 *
 * Returns: opaque id of #GstAnalyticsMtd type
 *
 * Since: 1.26
 */
GstAnalyticsMtdType gst_analytics_keypoint_group_mtd_get_mtd_type(void) {
    return (GstAnalyticsMtdType)&keypoint_group_impl;
}

/**
 * gst_analytics_keypointgroup_mtd_get_count:
 * @handle: instance handle
 *
 * Get number of keypoints in a keypoint group.
 * Returns: number of keypoints in this instance of keypoint metadata
 *
 * Since: 1.26
 */
gsize gst_analytics_keypointgroup_mtd_get_count(const GstAnalyticsKeypointGroupMtd *handle) {
    g_return_val_if_fail(handle, 0);
    g_return_val_if_fail(handle->meta != NULL, 0);

    GstAnalyticsKeypointGroupData *keypoints_mtd_data =
        (GstAnalyticsKeypointGroupData *)gst_analytics_relation_meta_get_mtd_data(handle->meta, handle->id);
    g_return_val_if_fail(keypoints_mtd_data != NULL, 0);

    return keypoints_mtd_data->count;
}

/**
 * gst_analytics_keypointgroup_mtd_get_keypoint_mtd:
 * @handle: handle to keypoint group metadata
 * @keypoint_mtd: (out caller-allocates): handle to keypoint metadata at @index position in this keypoint group
 * @index: keypoint index, must be < gst_analytics_keypointgroup_mtd_get_keypoints_count
 *
 * Get keypoint metadata handle at @index
 * Returns: TRUE if valid keypoint handle returned, FALSE if the call failed
 *
 * Since: 1.26
 */

gboolean gst_analytics_keypointgroup_mtd_get_keypoint_mtd(const GstAnalyticsKeypointGroupMtd *handle,
                                                          GstAnalyticsKeypointMtd *keypoint_mtd, gsize index) {
    g_return_val_if_fail(handle, FALSE);
    g_return_val_if_fail(handle->meta != NULL, FALSE);

    GstAnalyticsKeypointGroupData *keypoint_group_data =
        (GstAnalyticsKeypointGroupData *)gst_analytics_relation_meta_get_mtd_data(handle->meta, handle->id);
    g_return_val_if_fail(keypoint_group_data != NULL, FALSE);
    g_return_val_if_fail(keypoint_group_data->count > index, FALSE);

    // return keypoint metadata stored at index
    keypoint_mtd->meta = handle->meta;
    keypoint_mtd->id = keypoint_group_data->ids[index];

    return TRUE;
}

/**
 * gst_analytics_relation_meta_add_keypointgroup_mtd:
 * @instance: Instance of #GstAnalyticsRelationMeta where to add classification instance
 * @keypoint_count: number of keypoints to add
 * @keypoints: (array length=keypoint_count): metadata handles for individual keypoints
 * @keypoint_group_mtd: (out caller-allocates) (not nullable): Handle updated to newly added keypoint group meta.
 *
 * Add analytic keypoint metadata to @instance.
 * Returns: TRUE if valid keypoint group handle returned, FALSE if the call failed
 *
 * Since: 1.26
 */
gboolean gst_analytics_relation_meta_add_keypointgroup_mtd(GstAnalyticsRelationMeta *instance,
                                                           const gsize keypoint_count,
                                                           const GstAnalyticsKeypointMtd *keypoints,
                                                           GstAnalyticsKeypointGroupMtd *keypoint_group_mtd) {
    g_return_val_if_fail(instance != NULL, FALSE);
    g_return_val_if_fail(keypoints != NULL, FALSE);

    // create medatada for individual keypoints in a group and store metadata id
    gsize data_size = sizeof(guint) * keypoint_count;
    gsize size = sizeof(GstAnalyticsKeypointGroupData) + data_size;
    GstAnalyticsKeypointGroupData *keypoint_group_data =
        (GstAnalyticsKeypointGroupData *)gst_analytics_relation_meta_add_mtd(instance, &keypoint_group_impl, size,
                                                                             (GstAnalyticsMtd *)keypoint_group_mtd);
    g_return_val_if_fail(keypoint_group_data != NULL, FALSE);

    // store ids of individual keypoints in data array
    // metadata instance must be same for keypoints and keypoint group
    keypoint_group_data->count = keypoint_count;
    for (gsize k = 0; k < keypoint_count; k++) {
        g_return_val_if_fail(keypoints->meta == instance, FALSE);
        keypoint_group_data->ids[k] = keypoints[k].id;
    }

    return TRUE;
}

/**
 * gst_analytics_relation_meta_get_keypointgroup_mtd:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @an_meta_id: Id of #GstAnalyticsKeypointGroupMtd instance to retrieve
 * @rlt: (out caller-allocates)(not nullable): Will be filled with relatable meta
 *
 * Fill @rlt if a analytics-meta with id == @an_meta_id exist in @meta instance,
 * otherwise this method return FALSE and @rlt is invalid.
 *
 * Returns: TRUE if successful.
 *
 * Since: 1.26
 */
GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_get_keypointgroup_mtd(GstAnalyticsRelationMeta *meta, guint an_meta_id,
                                                           GstAnalyticsKeypointGroupMtd *rlt) {
    return gst_analytics_relation_meta_get_mtd(meta, an_meta_id, gst_analytics_keypointgroup_mtd_get_mtd_type(),
                                               (GstAnalyticsMtd *)rlt);
}

/**
 * gst_analytics_relation_meta_set_keypointgroup_relations:
 * @instance: Instance of #GstAnalyticsRelationMeta where to add classification instance.
 * @keypoint_group: Handle to keypoint group metadata.
 * @keypoint_names: (nullable): Handle to classification metadata with keypoint names.
 * @keypoint_skeleton: (nullable): Handle to skeleton metadata with skeleton segments.
 *
 * Setup default relations between keypoints, keypoint group, keypoint names and skeleton.
 *
 * keypoint_group (1) --> GST_ANALYTICS_REL_TYPE_CONTAIN    --> keypoint (N)
 * keypoint_group (1) <-- GST_ANALYTICS_REL_TYPE_IS_PART_OF <-- keypoint (N)
 * keypoint_group (M) --> GST_ANALYTICS_REL_TYPE_CATEGORY   <-- keypoint_names (1)
 * keypoint_group (M) --> GST_ANALYTICS_REL_TYPE_CATEGORY   <-- keypoint_skeleton (1)
 *
 * keypoint_names and keypoint_skeleton - one instance per frame
 * keypoint_group - M instances per frame (for each detectek keypoint group)
 * keypoint - N instances per keypoint group (MxN instances per frame)
 *
 * Returns: TRUE if relations added successfully, FALSE if the call fails
 *
 * Since: 1.26
 */

GST_ANALYTICS_META_API
gboolean gst_analytics_relation_meta_set_keypointgroup_relations(GstAnalyticsRelationMeta *instance,
                                                                 GstAnalyticsKeypointGroupMtd *keypoint_group,
                                                                 GstAnalyticsClsMtd *keypoint_names,
                                                                 GstAnalyticsKeypointSkeletonMtd *keypoint_skeleton) {

    g_return_val_if_fail(instance != NULL, FALSE);
    g_return_val_if_fail(keypoint_group != NULL, FALSE);
    g_return_val_if_fail(keypoint_group->meta == instance, FALSE);

    GstAnalyticsKeypointGroupData *keypoint_group_data =
        (GstAnalyticsKeypointGroupData *)gst_analytics_relation_meta_get_mtd_data(keypoint_group->meta,
                                                                                  keypoint_group->id);
    g_return_val_if_fail(keypoint_group_data != NULL, FALSE);

    // set bi-directional relationship between keypoints and keypoint group
    for (gsize k = 0; k < keypoint_group_data->count; k++) {
        if (!gst_analytics_relation_meta_set_relation(instance, GST_ANALYTICS_REL_TYPE_CONTAIN, keypoint_group->id,
                                                      keypoint_group_data->ids[k]))
            return FALSE;
        if (!gst_analytics_relation_meta_set_relation(instance, GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                                      keypoint_group_data->ids[k], keypoint_group->id))
            return FALSE;
    }

    // set category relationship between keypoint group and keypoint names/skeletons
    // TODO - add new relation type: GST_ANALYTICS_REL_TYPE_CONTAIN
    if (keypoint_names != NULL) {
        g_return_val_if_fail(gst_analytics_cls_mtd_get_length(keypoint_names) == keypoint_group_data->count, FALSE);
        g_return_val_if_fail(keypoint_names->meta == instance, FALSE);
        if (!gst_analytics_relation_meta_set_relation(instance, GST_ANALYTICS_REL_TYPE_RELATE_TO, keypoint_group->id,
                                                      keypoint_names->id))
            return FALSE;
    }
    if (keypoint_skeleton != NULL) {
        g_return_val_if_fail(keypoint_skeleton->meta == instance, FALSE);
        if (!gst_analytics_relation_meta_set_relation(instance, GST_ANALYTICS_REL_TYPE_RELATE_TO, keypoint_group->id,
                                                      keypoint_skeleton->id))
            return FALSE;
    }

    return TRUE;
}
