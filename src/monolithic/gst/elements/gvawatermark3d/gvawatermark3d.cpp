/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gvawatermark3d.h"
#include <fstream>
#include <gst/gst.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/video.h>
#include <nlohmann/json.hpp>
#include <opencv2/core/quaternion.hpp>

enum {
    PROP_0,
    PROP_INTRINSICS_FILE,
};

// A generic fallback camera intrinsics matrix (e.g., 1920x1080, fx=fy=1000, cx=960, cy=540)
static const cv::Mat DEFAULT_INTRINSICS =
    (cv::Mat_<double>(3, 3) << 1000.0, 0.0, 960.0, 0.0, 1000.0, 540.0, 0.0, 0.0, 1.0);

GST_DEBUG_CATEGORY_STATIC(gst_gvawatermark3d_debug_category);
#define GST_CAT_DEFAULT gst_gvawatermark3d_debug_category

/* Pad templates */
static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

G_DEFINE_TYPE(GstGvaWatermark3D, gst_gvawatermark3d, GST_TYPE_VIDEO_FILTER)

static cv::Mat load_intrinsics_matrix(const gchar *filename) {
    std::ifstream f(filename);
    if (!f.is_open())
        return cv::Mat();
    nlohmann::json j;
    f >> j;
    if (!j.contains("intrinsic_matrix"))
        return cv::Mat();
    auto mat = j["intrinsic_matrix"];
    cv::Mat K(3, 3, CV_64F);
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k)
            K.at<double>(i, k) = mat[i][k];
    return K;
}

// Helper: project 3D points to image
static void project_to_image(const std::vector<cv::Point3f> &pts3d, std::vector<cv::Point2i> &pts2d, const cv::Mat &K) {
    std::vector<cv::Point2f> pts2f;
    cv::Mat rvec = cv::Mat::zeros(3, 1, CV_64F);
    cv::Mat tvec = cv::Mat::zeros(3, 1, CV_64F);
    cv::projectPoints(pts3d, rvec, tvec, K, cv::Mat(), pts2f);
    pts2d.clear();
    for (const auto &pt : pts2f)
        pts2d.emplace_back(cv::Point2i(cvRound(pt.x), cvRound(pt.y)));
}

// Helper: draw 3D bounding box, with the face having the smallest average z in red
static void draw_3d_box(cv::Mat &img, const std::vector<float> &translation, const std::vector<float> &rotation,
                        const std::vector<float> &dimension, const cv::Mat &K) {
    float l = dimension[0], w_box = dimension[1], h = dimension[2];
    std::vector<cv::Point3f> local_corners = {{l / 2, w_box / 2, 0},   {l / 2, -w_box / 2, 0}, {-l / 2, -w_box / 2, 0},
                                              {-l / 2, w_box / 2, 0},  {l / 2, w_box / 2, h},  {l / 2, -w_box / 2, h},
                                              {-l / 2, -w_box / 2, h}, {-l / 2, w_box / 2, h}};

    // Rotation: [x, y, z, w] (scipy)
    double x = rotation[0], y = rotation[1], z = rotation[2], w_quat = rotation[3];
    cv::Matx33d Rm;
    {
        double xx = x * x, yy = y * y, zz = z * z;
        double xy = x * y, xz = x * z, yz = y * z;
        double wx = w_quat * x, wy = w_quat * y, wz = w_quat * z;
        Rm(0, 0) = 1 - 2 * (yy + zz);
        Rm(0, 1) = 2 * (xy - wz);
        Rm(0, 2) = 2 * (xz + wy);
        Rm(1, 0) = 2 * (xy + wz);
        Rm(1, 1) = 1 - 2 * (xx + zz);
        Rm(1, 2) = 2 * (yz - wx);
        Rm(2, 0) = 2 * (xz - wy);
        Rm(2, 1) = 2 * (yz + wx);
        Rm(2, 2) = 1 - 2 * (xx + yy);
    }

    std::vector<cv::Point3f> corners3d;
    for (const auto &pt : local_corners) {
        cv::Vec3d p(pt.x, pt.y, pt.z);
        cv::Vec3d rotated = Rm * p;
        corners3d.emplace_back(rotated[0] + translation[0], rotated[1] + translation[1], rotated[2] + translation[2]);
    }

    std::vector<cv::Point2i> corners2d;
    project_to_image(corners3d, corners2d, K);

    // Defensive: must have 8 points
    if (corners2d.size() < 8)
        return;

    // Defensive: check for finite points
    for (const auto &pt : corners2d) {
        if (!cv::checkRange(cv::Mat(pt)))
            return;
    }

    // Define the 6 faces by their 4 corner indices
    const int faces[6][4] = {
        {0, 1, 2, 3}, // bottom
        {4, 5, 6, 7}, // top
        {0, 1, 5, 4}, // front
        {2, 3, 7, 6}, // back
        {1, 2, 6, 5}, // right
        {0, 3, 7, 4}  // left
    };

    // Find the face with the smallest average z
    int min_face = 0;
    double min_z = std::numeric_limits<double>::max();
    for (int f = 0; f < 6; ++f) {
        double z = 0;
        for (int i = 0; i < 4; ++i)
            z += corners3d[faces[f][i]].z;
        z /= 4.0;
        if (z < min_z) {
            min_z = z;
            min_face = f;
        }
    }

    cv::Scalar color_face(0, 0, 255); // Red for closest face
    cv::Scalar color_box(0, 255, 0);  // Green for other edges

    // Draw all box edges in green
    int box_idxs[] = {0, 1, 2, 3, 7, 6, 5, 4, 7, 3, 0, 4, 5, 1, 2, 6};
    for (int i = 0; i < 15; ++i) {
        if (box_idxs[i] < 0 || box_idxs[i] >= 8 || box_idxs[i + 1] < 0 || box_idxs[i + 1] >= 8)
            continue;
        cv::line(img, corners2d[box_idxs[i]], corners2d[box_idxs[i + 1]], color_box, 2, cv::LINE_AA);
    }

    // Draw the closest face in red (overwriting green if overlapping)
    for (int i = 0; i < 4; ++i) {
        int idx0 = faces[min_face][i];
        int idx1 = faces[min_face][(i + 1) % 4];
        if (idx0 < 0 || idx0 >= 8 || idx1 < 0 || idx1 >= 8)
            continue;
        cv::line(img, corners2d[idx0], corners2d[idx1], color_face, 2, cv::LINE_AA);
    }
}

static void gst_gvawatermark3d_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaWatermark3D *self = GST_GVAWATERMARK3D(object);
    switch (prop_id) {
    case PROP_INTRINSICS_FILE:
        g_free(self->intrinsics_file);
        self->intrinsics_file = g_value_dup_string(value);
        if (self->intrinsics_file && strlen(self->intrinsics_file) > 0) {
            self->K = load_intrinsics_matrix(self->intrinsics_file);
            if (self->K.empty()) {
                GST_WARNING("Failed to load intrinsic matrix from %s", self->intrinsics_file);
            }
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gvawatermark3d_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaWatermark3D *self = GST_GVAWATERMARK3D(object);
    switch (prop_id) {
    case PROP_INTRINSICS_FILE:
        g_value_set_string(value, self->intrinsics_file);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static GstFlowReturn gst_gvawatermark3d_transform_frame(GstVideoFilter *filter, GstVideoFrame *inframe,
                                                        GstVideoFrame *outframe) {
    (void)filter;

    GstMapInfo in_map, out_map;
    if (!gst_buffer_map(inframe->buffer, &in_map, GST_MAP_READ))
        return GST_FLOW_ERROR;
    if (!gst_buffer_map(outframe->buffer, &out_map, GST_MAP_WRITE)) {
        gst_buffer_unmap(inframe->buffer, &in_map);
        return GST_FLOW_ERROR;
    }

    int width = GST_VIDEO_FRAME_WIDTH(inframe);
    int height = GST_VIDEO_FRAME_HEIGHT(inframe);

    cv::Mat input(height, width, CV_8UC3, (void *)in_map.data);
    cv::Mat output = input.clone();

    // Camera intrinsics (replace with your real values if needed)
    GstGvaWatermark3D *self = GST_GVAWATERMARK3D(filter);

    // Use loaded K if available, otherwise fallback
    cv::Mat K = self->K.empty() ? DEFAULT_INTRINSICS : self->K;

    GstMeta *meta;
    gpointer state = NULL;
    while ((meta = gst_buffer_iterate_meta(inframe->buffer, &state))) {
        if (meta->info->api == GST_VIDEO_REGION_OF_INTEREST_META_API_TYPE) {
            GstVideoRegionOfInterestMeta *roi_meta = (GstVideoRegionOfInterestMeta *)meta;
            for (GList *l = roi_meta->params; l != NULL; l = l->next) {
                GstStructure *structure = (GstStructure *)l->data;
                if (g_strcmp0(gst_structure_get_name(structure), "detection") == 0) {
                    // 1. Extract normalized ROI coordinates
                    double x_min = 0, x_max = 0, y_min = 0, y_max = 0;
                    gst_structure_get_double(structure, "x_min", &x_min);
                    gst_structure_get_double(structure, "x_max", &x_max);
                    gst_structure_get_double(structure, "y_min", &y_min);
                    gst_structure_get_double(structure, "y_max", &y_max);

                    int roi_x = static_cast<int>(x_min * width);
                    int roi_y = static_cast<int>(y_min * height);
                    int roi_w = static_cast<int>((x_max - x_min) * width);
                    int roi_h = static_cast<int>((y_max - y_min) * height);

                    // 2. Extract extra_params_json
                    std::vector<float> translation, rotation, dimension;
                    if (gst_structure_has_field(structure, "extra_params_json")) {
                        const GValue *val = gst_structure_get_value(structure, "extra_params_json");
                        if (G_VALUE_HOLDS_STRING(val)) {
                            const gchar *json_str = g_value_get_string(val);
                            if (json_str && strlen(json_str) > 0) {
                                try {
                                    nlohmann::json root = nlohmann::json::parse(json_str);
                                    if (root.contains("translation") && root["translation"].is_array())
                                        for (const auto &v : root["translation"])
                                            translation.push_back(v.get<float>());
                                    if (root.contains("rotation") && root["rotation"].is_array())
                                        for (const auto &v : root["rotation"])
                                            rotation.push_back(v.get<float>());
                                    if (root.contains("dimension") && root["dimension"].is_array())
                                        for (const auto &v : root["dimension"])
                                            dimension.push_back(v.get<float>());
                                } catch (const std::exception &e) {
                                    g_print("gvadeskew: Failed to parse extra_params_json: %s\n", e.what());
                                }
                            }
                        }
                    }

                    // 3. Only process if all params are present and ROI is valid
                    if (translation.size() == 3 && rotation.size() == 4 && dimension.size() == 3 && roi_w > 0 &&
                        roi_h > 0 && roi_x >= 0 && roi_y >= 0 && roi_x + roi_w <= width && roi_y + roi_h <= height) {

                        // Draw 3D bounding box (like plot.py)
                        draw_3d_box(output, translation, rotation, dimension, K);
                    }
                }
            }
        }
    }

    memcpy(out_map.data, output.data, output.total() * output.elemSize());
    gst_buffer_copy_into(outframe->buffer, inframe->buffer, GST_BUFFER_COPY_META, 0, -1);
    gst_buffer_unmap(inframe->buffer, &in_map);
    gst_buffer_unmap(outframe->buffer, &out_map);
    return GST_FLOW_OK;
}
//
static void gst_gvawatermark3d_finalize(GObject *object);

static void gst_gvawatermark3d_class_init(GstGvaWatermark3DClass *klass) {
    GST_DEBUG_CATEGORY_INIT(gst_gvawatermark3d_debug_category, "gvawatermark3d", 0, "3D Watermark video filter");
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

    gst_element_class_set_static_metadata(element_class,
                                          "3D Watermark video filter", // long name
                                          "Filter/Effect/Video", "Draws 3D watermarks on video frames",
                                          "Intel® Corporation");

    video_filter_class->transform_frame = gst_gvawatermark3d_transform_frame;

    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->set_property = gst_gvawatermark3d_set_property;
    gobject_class->get_property = gst_gvawatermark3d_get_property;
    gobject_class->finalize = gst_gvawatermark3d_finalize;

    g_object_class_install_property(gobject_class, PROP_INTRINSICS_FILE,
                                    g_param_spec_string("intrinsics-file", "Intrinsics File",
                                                        "Path to JSON file with camera intrinsics", NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_gvawatermark3d_init(GstGvaWatermark3D *self) {
    self->intrinsics_file = NULL;
    self->K = cv::Mat();
}

static void gst_gvawatermark3d_finalize(GObject *object) {
    GstGvaWatermark3D *self = GST_GVAWATERMARK3D(object);
    g_free(self->intrinsics_file);
    G_OBJECT_CLASS(gst_gvawatermark3d_parent_class)->finalize(object);
}
