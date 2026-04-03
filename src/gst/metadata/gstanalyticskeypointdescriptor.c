/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * SECTION:gstanalyticskeypointdescriptor
 * @title: GstAnalyticsKeypointDescriptor
 * @short_description: Keypoint layout descriptors with semantic tags
 * @see_also: #GstAnalyticsKeypointMtd, #GstAnalyticsGroupMtd
 *
 * #GstAnalyticsKeypointDescriptor associates a semantic tag with a set of
 * named keypoints and skeleton connections.  Use
 * gst_analytics_keypoint_descriptor_lookup() to find a descriptor by its
 * semantic tag, then use the accessor functions to query point names and
 * skeleton edges.
 */

#include "dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h"

#include <string.h>

/* ===== COCO 17-keypoint body pose ===== */

static const char *const _coco17_point_names[] = {
    "nose",    "eye_l",   "eye_r", "ear_l", "ear_r",  "shoulder_l", "shoulder_r", "elbow_l", "elbow_r",
    "wrist_l", "wrist_r", "hip_l", "hip_r", "knee_l", "knee_r",     "ankle_l",    "ankle_r"};

static const gint _coco17_skeleton[] = {
    0,  1,  /* nose        - eye_l       */
    0,  2,  /* nose        - eye_r       */
    1,  3,  /* eye_l       - ear_l       */
    2,  4,  /* eye_r       - ear_r       */
    3,  5,  /* ear_l       - shoulder_l  */
    4,  6,  /* ear_r       - shoulder_r  */
    5,  6,  /* shoulder_l  - shoulder_r  */
    5,  7,  /* shoulder_l  - elbow_l     */
    6,  8,  /* shoulder_r  - elbow_r     */
    7,  9,  /* elbow_l     - wrist_l     */
    8,  10, /* elbow_r     - wrist_r     */
    5,  11, /* shoulder_l  - hip_l       */
    6,  12, /* shoulder_r  - hip_r       */
    11, 12, /* hip_l       - hip_r       */
    11, 13, /* hip_l       - knee_l      */
    12, 14, /* hip_r       - knee_r      */
    13, 15, /* knee_l      - ankle_l     */
    14, 16  /* knee_r      - ankle_r     */
};

static const GstAnalyticsKeypointDescriptor _descriptor_coco17 = {
    GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17, _coco17_point_names, 17,
    _coco17_skeleton, 18};

/* ===== OpenPose 18-keypoint body pose ===== */

static const char *const _openpose18_point_names[] = {
    "nose",   "neck",    "shoulder_r", "elbow_r", "wrist_r", "shoulder_l", "elbow_l", "wrist_l", "hip_r",
    "knee_r", "ankle_r", "hip_l",      "knee_l",  "ankle_l", "eye_r",      "eye_l",   "ear_r",   "ear_l"};

static const gint _openpose18_skeleton[] = {
    5,  2,  /* shoulder_l  - shoulder_r  */
    0,  15, /* nose        - eye_l       */
    0,  14, /* nose        - eye_r       */
    15, 17, /* eye_l       - ear_l       */
    14, 16, /* eye_r       - ear_r       */
    6,  5,  /* elbow_l     - shoulder_l  */
    3,  2,  /* elbow_r     - shoulder_r  */
    7,  6,  /* wrist_l     - elbow_l     */
    4,  3,  /* wrist_r     - elbow_r     */
    11, 12, /* hip_l       - knee_l      */
    8,  9,  /* hip_r       - knee_r      */
    12, 13, /* knee_l      - ankle_l     */
    9,  10  /* knee_r      - ankle_r     */
};

static const GstAnalyticsKeypointDescriptor _descriptor_openpose18 = {
    GST_ANALYTICS_KEYPOINT_BODY_POSE_OPENPOSE_18, _openpose18_point_names, 18,
    _openpose18_skeleton, 13};

/* ===== HRNet COCO 17-keypoint body pose ===== */

static const gint _hrnet_coco17_skeleton[] = {
    5,  6,  /* shoulder_l  - shoulder_r  */
    0,  1,  /* nose        - eye_l       */
    0,  2,  /* nose        - eye_r       */
    1,  3,  /* eye_l       - ear_l       */
    2,  4,  /* eye_r       - ear_r       */
    7,  5,  /* elbow_l     - shoulder_l  */
    8,  6,  /* elbow_r     - shoulder_r  */
    9,  7,  /* wrist_l     - elbow_l     */
    10, 8,  /* wrist_r     - elbow_r     */
    11, 13, /* hip_l       - knee_l      */
    12, 14, /* hip_r       - knee_r      */
    13, 15, /* knee_l      - ankle_l     */
    14, 16  /* knee_r      - ankle_r     */
};

static const GstAnalyticsKeypointDescriptor _descriptor_hrnet_coco17 = {
    GST_ANALYTICS_KEYPOINT_BODY_POSE_HRNET_COCO_17, _coco17_point_names, 17,
    _hrnet_coco17_skeleton, 13};

/* ===== CenterFace 5-point facial landmarks ===== */

static const char *const _centerface5_point_names[] = {
    "eye_l", "eye_r", "nose_tip", "mouth_l", "mouth_r"};

static const GstAnalyticsKeypointDescriptor _descriptor_centerface5 = {
    GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5, _centerface5_point_names, 5,
    NULL, 0};

/* Registry of all known keypoint descriptors */
static const GstAnalyticsKeypointDescriptor *const _descriptors[] = {
    &_descriptor_coco17,
    &_descriptor_openpose18,
    &_descriptor_hrnet_coco17,
    &_descriptor_centerface5,
    NULL
};

/* ===== Public API ===== */

/**
 * gst_analytics_keypoint_descriptor_lookup:
 * @semantic_tag: The semantic tag to look up.
 *
 * Search all built-in keypoint descriptors for one whose semantic tag
 * matches @semantic_tag exactly.
 *
 * Returns: (transfer none) (nullable): the matching
 *   #GstAnalyticsKeypointDescriptor, or %NULL if not found.
 */
const GstAnalyticsKeypointDescriptor *
gst_analytics_keypoint_descriptor_lookup (const gchar *semantic_tag)
{
    if (!semantic_tag)
        return NULL;

    for (const GstAnalyticsKeypointDescriptor *const *it = _descriptors; *it; ++it) {
        if ((*it)->semantic_tag && strcmp ((*it)->semantic_tag, semantic_tag) == 0)
            return *it;
    }
    return NULL;
}

/**
 * gst_analytics_keypoint_descriptor_get_semantic_tag:
 * @desc: a #GstAnalyticsKeypointDescriptor
 *
 * Get the semantic tag string that uniquely identifies the keypoint layout.
 *
 * Returns: (transfer none) (nullable): the semantic tag, or %NULL.
 */
const gchar *
gst_analytics_keypoint_descriptor_get_semantic_tag (const GstAnalyticsKeypointDescriptor *desc)
{
    g_return_val_if_fail (desc != NULL, NULL);
    return desc->semantic_tag;
}

/**
 * gst_analytics_keypoint_descriptor_get_point_count:
 * @desc: a #GstAnalyticsKeypointDescriptor
 *
 * Get the number of keypoints described by @desc.
 *
 * Returns: the number of keypoints.
 */
gsize
gst_analytics_keypoint_descriptor_get_point_count (const GstAnalyticsKeypointDescriptor *desc)
{
    g_return_val_if_fail (desc != NULL, 0);
    return desc->point_count;
}

/**
 * gst_analytics_keypoint_descriptor_get_point_name:
 * @desc: a #GstAnalyticsKeypointDescriptor
 * @index: index of the keypoint (must be less than the point count)
 *
 * Get the name of the keypoint at @index.
 *
 * Returns: (transfer none) (nullable): the keypoint name, or %NULL if
 *   @index is out of range.
 */
const gchar *
gst_analytics_keypoint_descriptor_get_point_name (const GstAnalyticsKeypointDescriptor *desc,
                                                  gsize index)
{
    g_return_val_if_fail (desc != NULL, NULL);
    if (index >= desc->point_count)
        return NULL;
    return desc->point_names[index];
}

/**
 * gst_analytics_keypoint_descriptor_get_skeleton_connection_count:
 * @desc: a #GstAnalyticsKeypointDescriptor
 *
 * Get the number of skeleton connection pairs in @desc.
 *
 * Returns: the number of skeleton connection pairs.
 */
gsize
gst_analytics_keypoint_descriptor_get_skeleton_connection_count (const GstAnalyticsKeypointDescriptor *desc)
{
    g_return_val_if_fail (desc != NULL, 0);
    return desc->skeleton_connection_count;
}

/**
 * gst_analytics_keypoint_descriptor_get_skeleton_connection:
 * @desc: a #GstAnalyticsKeypointDescriptor
 * @index: index of the connection pair (must be less than the skeleton
 *   connection count)
 * @from_idx: (out): index of the source keypoint
 * @to_idx: (out): index of the destination keypoint
 *
 * Get a skeleton connection at @index.  Each connection is a directed edge
 * between two keypoints identified by their indices into the point_names
 * array.
 *
 * Returns: %TRUE if the connection was retrieved successfully,
 *   %FALSE if @index is out of range or @desc has no skeleton.
 */
gboolean
gst_analytics_keypoint_descriptor_get_skeleton_connection (const GstAnalyticsKeypointDescriptor *desc,
                                                           gsize index,
                                                           gint *from_idx,
                                                           gint *to_idx)
{
    g_return_val_if_fail (desc != NULL, FALSE);
    g_return_val_if_fail (from_idx != NULL, FALSE);
    g_return_val_if_fail (to_idx != NULL, FALSE);

    if (index >= desc->skeleton_connection_count || desc->skeleton_connections == NULL)
        return FALSE;

    *from_idx = desc->skeleton_connections[index * 2];
    *to_idx = desc->skeleton_connections[index * 2 + 1];
    return TRUE;
}
