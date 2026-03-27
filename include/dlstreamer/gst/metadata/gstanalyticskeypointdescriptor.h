/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef __GST_ANALYTICS_KEYPOINT_DESCRIPTOR_H__
#define __GST_ANALYTICS_KEYPOINT_DESCRIPTOR_H__

#include <stddef.h>

/**
 * GstAnalyticsKeypointDescriptor:
 * @semantic_tag: Unique identifier string for this keypoint format.
 * @point_names: Array of keypoint name strings.
 * @point_count: Number of keypoints in @point_names.
 * @skeleton_connections: Array of int pairs (index_a, index_b) into
 *   @point_names describing the skeleton edges. Length is
 *   @skeleton_connection_count * 2.
 * @skeleton_connection_count: Number of skeleton connection pairs.
 *
 * Descriptor that associates a semantic tag with a set of keypoint names
 * and their skeleton connections, so that code consuming keypoint metadata
 * can look up the layout by tag.
 */
typedef struct {
    const char *semantic_tag;
    const char *const *point_names;
    size_t point_count;
    const int *skeleton_connections;
    size_t skeleton_connection_count;
} GstAnalyticsKeypointDescriptor;

/*
 * COCO 17-keypoint body pose
 *
 * Keypoint indices:
 *   0  nose           1  eye_l          2  eye_r
 *   3  ear_l          4  ear_r          5  shoulder_l
 *   6  shoulder_r     7  elbow_l        8  elbow_r
 *   9  wrist_l       10  wrist_r       11  hip_l
 *  12  hip_r         13  knee_l        14  knee_r
 *  15  ankle_l       16  ankle_r
 */

#define GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17 "body-pose/coco-17"

static const char *const _gst_keypoint_coco17_point_names[] = {
    "nose",    "eye_l",   "eye_r",  "ear_l",  "ear_r",
    "shoulder_l", "shoulder_r", "elbow_l", "elbow_r",
    "wrist_l", "wrist_r", "hip_l",  "hip_r",
    "knee_l",  "knee_r",  "ankle_l", "ankle_r"
};

/* Each consecutive pair of ints is (from, to) using the indices above. */
static const int _gst_keypoint_coco17_skeleton[] = {
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
     8, 10,  /* elbow_r     - wrist_r     */
     5, 11,  /* shoulder_l  - hip_l       */
     6, 12,  /* shoulder_r  - hip_r       */
    11, 12,  /* hip_l       - hip_r       */
    11, 13,  /* hip_l       - knee_l      */
    12, 14,  /* hip_r       - knee_r      */
    13, 15,  /* knee_l      - ankle_l     */
    14, 16   /* knee_r      - ankle_r     */
};

static const GstAnalyticsKeypointDescriptor gst_keypoint_descriptor_coco17 = {
    GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17,
    _gst_keypoint_coco17_point_names,
    17,
    _gst_keypoint_coco17_skeleton,
    18
};

/*
 * OpenPose 18-keypoint body pose (COCO + neck)
 *
 * Keypoint indices:
 *   0  nose           1  neck           2  shoulder_r
 *   3  elbow_r        4  wrist_r        5  shoulder_l
 *   6  elbow_l        7  wrist_l        8  hip_r
 *   9  knee_r        10  ankle_r       11  hip_l
 *  12  knee_l        13  ankle_l       14  eye_r
 *  15  eye_l         16  ear_r         17  ear_l
 */

#define GST_ANALYTICS_KEYPOINT_BODY_POSE_OPENPOSE_18 "body-pose/openpose-18"

static const char *const _gst_keypoint_openpose18_point_names[] = {
    "nose",       "neck",       "shoulder_r", "elbow_r",  "wrist_r",
    "shoulder_l", "elbow_l",    "wrist_l",    "hip_r",    "knee_r",
    "ankle_r",    "hip_l",      "knee_l",     "ankle_l",
    "eye_r",      "eye_l",      "ear_r",      "ear_l"
};

/* Skeleton from OpenPose limb_ids_heatmap (peak.cpp). */
static const int _gst_keypoint_openpose18_skeleton[] = {
     1,  2,  /* neck        - shoulder_r  */
     1,  5,  /* neck        - shoulder_l  */
     2,  3,  /* shoulder_r  - elbow_r     */
     3,  4,  /* elbow_r     - wrist_r     */
     5,  6,  /* shoulder_l  - elbow_l     */
     6,  7,  /* elbow_l     - wrist_l     */
     1,  8,  /* neck        - hip_r       */
     8,  9,  /* hip_r       - knee_r      */
     9, 10,  /* knee_r      - ankle_r     */
     1, 11,  /* neck        - hip_l       */
    11, 12,  /* hip_l       - knee_l      */
    12, 13,  /* knee_l      - ankle_l     */
     1,  0,  /* neck        - nose        */
     0, 14,  /* nose        - eye_r       */
    14, 16,  /* eye_r       - ear_r       */
     0, 15,  /* nose        - eye_l       */
    15, 17   /* eye_l       - ear_l       */
};

static const GstAnalyticsKeypointDescriptor gst_keypoint_descriptor_openpose18 = {
    GST_ANALYTICS_KEYPOINT_BODY_POSE_OPENPOSE_18,
    _gst_keypoint_openpose18_point_names,
    18,
    _gst_keypoint_openpose18_skeleton,
    17
};

/*
 * CenterFace 5-point facial landmarks
 *
 * Keypoint indices:
 *   0  eye_l          1  eye_r          2  nose_tip
 *   3  mouth_l        4  mouth_r
 */

#define GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5 "face-landmarks/centerface-5"

static const char *const _gst_keypoint_centerface5_point_names[] = {
    "eye_l", "eye_r", "nose_tip", "mouth_l", "mouth_r"
};

static const GstAnalyticsKeypointDescriptor gst_keypoint_descriptor_centerface5 = {
    GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5,
    _gst_keypoint_centerface5_point_names,
    5,
    NULL,
    0
};

/* Registry of all known keypoint descriptors. Add new entries here. */
static const GstAnalyticsKeypointDescriptor *const _gst_keypoint_descriptors[] = {
    &gst_keypoint_descriptor_coco17,
    &gst_keypoint_descriptor_openpose18,
    &gst_keypoint_descriptor_centerface5,
    NULL
};

/**
 * gst_analytics_keypoint_descriptor_find_by_tag:
 * @semantic_tag: The tag to look up.
 *
 * Search all known keypoint descriptors for a matching @semantic_tag.
 *
 * Returns: pointer to the matching descriptor, or %NULL if not found.
 */
static inline const GstAnalyticsKeypointDescriptor *
gst_analytics_keypoint_descriptor_find_by_tag(const char *semantic_tag)
{
    if (!semantic_tag)
        return NULL;
    const GstAnalyticsKeypointDescriptor *const *it = _gst_keypoint_descriptors;
    for (; *it; ++it) {
        const char *a = (*it)->semantic_tag;
        const char *b = semantic_tag;
        if (a) {
            while (*a && *a == *b) { ++a; ++b; }
            if (*a == '\0' && *b == '\0')
                return *it;
        }
    }
    return NULL;
}

#endif /* __GST_ANALYTICS_KEYPOINT_DESCRIPTOR_H__ */
