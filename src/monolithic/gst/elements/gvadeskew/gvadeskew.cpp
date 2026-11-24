/*******************************************************************************
 * Copyright (C) 2025-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gvadeskew.h"
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

static const cv::Mat DEFAULT_INTRINSICS =
    (cv::Mat_<double>(3, 3) << 1000.0, 0.0, 960.0, 0.0, 1000.0, 540.0, 0.0, 0.0, 1.0);

static void gst_gvadeskew_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_gvadeskew_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void gst_gvadeskew_finalize(GObject *object);

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

GST_DEBUG_CATEGORY_STATIC(gst_gvadeskew_debug_category);
#define GST_CAT_DEFAULT gst_gvadeskew_debug_category

/* Pad templates */
static GstStaticPadTemplate sink_template =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static GstStaticPadTemplate src_template =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

G_DEFINE_TYPE(GstGvaDeskew, gst_gvadeskew, GST_TYPE_VIDEO_FILTER)

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

// Helper: returns the four 2D points of the closest face (smallest average z) of the 3D bounding box,
// ordered as top-left, top-right, bottom-right, bottom-left in image coordinates.
static bool get_closest_face_points(const std::vector<float> &translation, const std::vector<float> &rotation,
                                    const std::vector<float> &dimension, const cv::Mat &K,
                                    std::vector<cv::Point2f> &face_points) {
    float l = dimension[0], w_box = dimension[1], h = dimension[2];
    std::vector<cv::Point3f> local_corners = {{l / 2, w_box / 2, 0},   {l / 2, -w_box / 2, 0}, {-l / 2, -w_box / 2, 0},
                                              {-l / 2, w_box / 2, 0},  {l / 2, w_box / 2, h},  {l / 2, -w_box / 2, h},
                                              {-l / 2, -w_box / 2, h}, {-l / 2, w_box / 2, h}};

    // Quaternion to rotation matrix
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

    std::vector<cv::Point2i> tmp2i;
    project_to_image(corners3d, tmp2i, K);
    std::vector<cv::Point2f> corners2d(tmp2i.begin(), tmp2i.end());
    if (corners2d.size() < 8)
        return false;

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

    // Get the 2D points of the closest face
    std::vector<cv::Point2f> pts;
    for (int i = 0; i < 4; ++i)
        pts.push_back(corners2d[faces[min_face][i]]);

    // Order: top-left, top-right, bottom-right, bottom-left
    // Find the top-left point (smallest sum), bottom-right (largest sum), etc.
    std::vector<cv::Point2f> ordered(4);
    std::vector<float> sums, diffs;
    for (const auto &pt : pts) {
        sums.push_back(pt.x + pt.y);
        diffs.push_back(pt.y - pt.x);
    }
    // top-left: min sum
    int idx_tl = std::min_element(sums.begin(), sums.end()) - sums.begin();
    // bottom-right: max sum
    int idx_br = std::max_element(sums.begin(), sums.end()) - sums.begin();
    // top-right: min diff
    int idx_tr = std::min_element(diffs.begin(), diffs.end()) - diffs.begin();
    // bottom-left: max diff
    int idx_bl = std::max_element(diffs.begin(), diffs.end()) - diffs.begin();

    ordered[0] = pts[idx_tl];
    ordered[1] = pts[idx_tr];
    ordered[2] = pts[idx_br];
    ordered[3] = pts[idx_bl];

    face_points = ordered;
    return true;
}

// Convert quaternion to rotation matrix
cv::Mat quaternionToRotationMatrix(const std::vector<float> &q) {
    float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
    cv::Mat R =
        (cv::Mat_<double>(3, 3) << 1 - 2 * qy * qy - 2 * qz * qz, 2 * qx * qy - 2 * qz * qw, 2 * qx * qz + 2 * qy * qw,
         2 * qx * qy + 2 * qz * qw, 1 - 2 * qx * qx - 2 * qz * qz, 2 * qy * qz - 2 * qx * qw, 2 * qx * qz - 2 * qy * qw,
         2 * qy * qz + 2 * qx * qw, 1 - 2 * qx * qx - 2 * qy * qy);
    return R;
}

void deskewAndPasteFace(cv::Mat &image, const std::vector<float> &translation, const std::vector<float> &rotation,
                        const std::vector<float> &dimension, const cv::Mat &K,
                        const std::vector<cv::Point2f> &facePoints, const cv::Rect &destinationRect) {
    // Convert quaternion to rotation matrix
    cv::Mat R_obj_to_cam = quaternionToRotationMatrix(rotation);
    cv::Mat t_obj_to_cam = (cv::Mat_<double>(3, 1) << translation[0], translation[1], translation[2]);

    float length = dimension[0], width = dimension[1], height = dimension[2];

    std::vector<cv::Point3f> objectFace = {{-length / 2, -height / 2, -width / 2},
                                           {length / 2, -height / 2, -width / 2},
                                           {length / 2, height / 2, -width / 2},
                                           {-length / 2, height / 2, -width / 2}};

    // Compute face center in camera coordinates
    cv::Mat faceCenterCam = R_obj_to_cam * (cv::Mat_<double>(3, 1) << 0, 0, -width / 2) + t_obj_to_cam;

    // Face normal in camera coordinates
    cv::Mat faceNormalCam = R_obj_to_cam * (cv::Mat_<double>(3, 1) << 0, 0, -1);
    faceNormalCam /= cv::norm(faceNormalCam);

    // Define virtual camera axes
    cv::Mat up = (cv::Mat_<double>(3, 1) << 0, -1, 0);
    cv::Mat x_axis = up.cross(faceNormalCam);
    x_axis /= cv::norm(x_axis);
    cv::Mat y_axis = faceNormalCam.cross(x_axis);

    cv::Mat R_virtual(3, 3, CV_64F);
    x_axis.copyTo(R_virtual.col(0));
    y_axis.copyTo(R_virtual.col(1));
    faceNormalCam.copyTo(R_virtual.col(2));

    // Virtual camera pose (inverse of world-to-virtual)
    cv::Mat rvec_virtual;
    cv::Rodrigues(R_virtual.t(), rvec_virtual);
    cv::Mat tvec_virtual = -R_virtual.t() * faceCenterCam;

    // Project 3D face points into virtual camera
    std::vector<cv::Point2f> rectifiedPoints;
    cv::projectPoints(objectFace, rvec_virtual, tvec_virtual, K, cv::Mat(), rectifiedPoints);

    // Compute bounding box and shift points to origin
    cv::Rect bbox = cv::boundingRect(rectifiedPoints);
    cv::Point2f offset(static_cast<float>(bbox.x), static_cast<float>(bbox.y));
    for (auto &pt : rectifiedPoints)
        pt -= offset;

    // Compute homography from original image to rectified view
    cv::Mat H = cv::getPerspectiveTransform(facePoints, rectifiedPoints);

    // Warp original image to rectified view
    cv::Mat rectified;
    cv::warpPerspective(image, rectified, H, bbox.size());

    // Define destination points based on the destination rectangle
    std::vector<cv::Point2f> destinationPoints = {
        cv::Point2f(static_cast<float>(destinationRect.x), static_cast<float>(destinationRect.y)),
        cv::Point2f(static_cast<float>(destinationRect.x + destinationRect.width),
                    static_cast<float>(destinationRect.y)),
        cv::Point2f(static_cast<float>(destinationRect.x + destinationRect.width),
                    static_cast<float>(destinationRect.y + destinationRect.height)),
        cv::Point2f(static_cast<float>(destinationRect.x),
                    static_cast<float>(destinationRect.y + destinationRect.height))};

    // Compute homography from rectified view to destination rectangle
    cv::Mat H_to_dest = cv::getPerspectiveTransform(rectifiedPoints, destinationPoints);

    // Warp rectified view into destination rectangle in original image
    cv::Mat warpedToDest;
    cv::warpPerspective(rectified, warpedToDest, H_to_dest, image.size(), cv::INTER_LINEAR, cv::BORDER_TRANSPARENT);

    // Create mask for blending
    cv::Mat mask = cv::Mat::zeros(image.size(), CV_8UC1);
    std::vector<std::vector<cv::Point>> roi = {
        std::vector<cv::Point>(destinationPoints.begin(), destinationPoints.end())};
    cv::fillPoly(mask, roi, cv::Scalar(255));

    // Paste rectified face into original image at the destination rectangle
    warpedToDest.copyTo(image, mask);
}

static GstFlowReturn gst_gvadeskew_transform_frame(GstVideoFilter *filter, GstVideoFrame *inframe,
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

    GstGvaDeskew *self = GST_GVADESKEW(filter);
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

                        // --- Draw the closest face quadrangle using helpers ---
                        std::vector<cv::Point2f> face_points;
                        if (get_closest_face_points(translation, rotation, dimension, K, face_points)) {
                            bool all_inside = true;
                            for (const auto &pt : face_points) {
                                if (pt.x < 0 || pt.x >= width || pt.y < 0 || pt.y >= height) {
                                    all_inside = false;
                                    break;
                                }
                            }
                            if (all_inside) {
                                cv::Rect destinationRect(roi_x, roi_y, roi_w, roi_h);
                                deskewAndPasteFace(output, translation, rotation, dimension, K, face_points,
                                                   destinationRect);
                            }
                        } else {
                            g_print("gvadeskew: Failed to get closest face points\n");
                        }
                    }
                }
            }
        }
    }

    gst_buffer_copy_into(outframe->buffer, inframe->buffer, GST_BUFFER_COPY_META, 0, -1);
    memcpy(out_map.data, output.data, output.total() * output.elemSize());
    gst_buffer_unmap(inframe->buffer, &in_map);
    gst_buffer_unmap(outframe->buffer, &out_map);
    return GST_FLOW_OK;
}

static void gst_gvadeskew_class_init(GstGvaDeskewClass *klass) {
    GST_DEBUG_CATEGORY_INIT(gst_gvadeskew_debug_category, "gvadeskew", 0, "Deskew video filter");
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstVideoFilterClass *video_filter_class = GST_VIDEO_FILTER_CLASS(klass);

    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

    gst_element_class_set_static_metadata(element_class,
                                          "Deskew video filter", // long name
                                          "Filter/Effect/Video", "Deskews video frames", "Intel® Corporation");

    video_filter_class->transform_frame = gst_gvadeskew_transform_frame;

    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->set_property = gst_gvadeskew_set_property;
    gobject_class->get_property = gst_gvadeskew_get_property;
    gobject_class->finalize = gst_gvadeskew_finalize;

    g_object_class_install_property(gobject_class, PROP_INTRINSICS_FILE,
                                    g_param_spec_string("intrinsics-file", "Intrinsics File",
                                                        "Path to JSON file with camera intrinsics", NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static void gst_gvadeskew_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) {
    GstGvaDeskew *self = GST_GVADESKEW(object);
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

static void gst_gvadeskew_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
    GstGvaDeskew *self = GST_GVADESKEW(object);
    switch (prop_id) {
    case PROP_INTRINSICS_FILE:
        g_value_set_string(value, self->intrinsics_file);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void gst_gvadeskew_finalize(GObject *object) {
    GstGvaDeskew *self = GST_GVADESKEW(object);
    g_free(self->intrinsics_file);
    G_OBJECT_CLASS(gst_gvadeskew_parent_class)->finalize(object);
}

static void gst_gvadeskew_init(GstGvaDeskew *self) {
    self->intrinsics_file = NULL;
    self->K = cv::Mat();
}
