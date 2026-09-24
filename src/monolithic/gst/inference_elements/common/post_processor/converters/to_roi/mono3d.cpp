/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "mono3d.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"

#include <openvino/core/type/float16.hpp>

#include <gst/gst.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace post_processing;

namespace {

constexpr size_t NUM_HEADING_BIN = 12;
constexpr double PI = 3.14159265358979323846;

inline float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Reads an output blob into a float vector, converting from FP16 when needed (MonoDETR heads are f16).
std::vector<float> blobToFloat(const InferenceBackend::OutputBlob::Ptr &blob) {
    if (!blob)
        throw std::invalid_argument("mono3d: output blob is nullptr.");
    const size_t n = blob->GetSize();
    std::vector<float> out(n);
    const void *data = blob->GetData();
    switch (blob->GetPrecision()) {
    case InferenceBackend::Blob::Precision::FP32: {
        const float *p = reinterpret_cast<const float *>(data);
        std::copy(p, p + n, out.begin());
        break;
    }
    case InferenceBackend::Blob::Precision::FP16: {
        const ov::float16 *p = reinterpret_cast<const ov::float16 *>(data);
        for (size_t i = 0; i < n; ++i)
            out[i] = static_cast<float>(p[i]);
        break;
    }
    default:
        throw std::runtime_error("mono3d: unsupported output precision (expected FP32 or FP16).");
    }
    return out;
}

// Inverse of angle2class: angle = cls * (2pi/bins) + residual, wrapped to (-pi, pi].
float class2angle(size_t cls, float residual) {
    const float angle_per_class = 2.0f * static_cast<float>(PI) / static_cast<float>(NUM_HEADING_BIN);
    float angle = static_cast<float>(cls) * angle_per_class + residual;
    if (angle > static_cast<float>(PI))
        angle -= 2.0f * static_cast<float>(PI);
    return angle;
}

// heading: 24 values = 12 bin logits + 12 residuals
float getHeadingAngle(const float *heading) {
    size_t best_bin = 0;
    float best_val = heading[0];
    for (size_t i = 1; i < NUM_HEADING_BIN; ++i) {
        if (heading[i] > best_val) {
            best_val = heading[i];
            best_bin = i;
        }
    }
    const float residual = heading[NUM_HEADING_BIN + best_bin];
    return class2angle(best_bin, residual);
}

} // namespace

Mono3DConverter::Calibration Mono3DConverter::readCalibration() const {
    Calibration calib{};
    // Defaults: pinhole-like intrinsics scaled to the model input, no translation.
    const auto &image_info = getModelInputImageInfo();
    const double def_w = static_cast<double>(image_info.width);
    const double def_h = static_cast<double>(image_info.height);
    calib.orig_width = def_w > 0 ? def_w : 1.0;
    calib.orig_height = def_h > 0 ? def_h : 1.0;
    calib.p2 = {def_w, 0, def_w / 2.0, 0, 0, def_h, def_h / 2.0, 0, 0, 0, 1, 0};

    const GstStructure *params = getModelProcOutputInfo().get();
    if (!params)
        return calib;

    GValueArray *arr = nullptr;
    if (gst_structure_get_array(const_cast<GstStructure *>(params), "P2", &arr) && arr) {
        if (arr->n_values == 12)
            for (guint i = 0; i < 12; ++i)
                calib.p2[i] = g_value_get_double(g_value_array_get_nth(arr, i));
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        g_value_array_free(arr);
        G_GNUC_END_IGNORE_DEPRECATIONS
    }

    int iw = 0, ih = 0;
    if (gst_structure_get_int(params, "orig_width", &iw) && iw > 0)
        calib.orig_width = static_cast<double>(iw);
    if (gst_structure_get_int(params, "orig_height", &ih) && ih > 0)
        calib.orig_height = static_cast<double>(ih);

    return calib;
}

size_t Mono3DConverter::readTopK() const {
    int topk = 50;
    const GstStructure *params = getModelProcOutputInfo().get();
    if (params)
        gst_structure_get_int(params, "topk", &topk);
    if (topk <= 0)
        topk = 50;
    return static_cast<size_t>(topk);
}

std::vector<float> Mono3DConverter::readClassMeanSizes(size_t num_classes) const {
    std::vector<float> mean_sizes(num_classes * 3, 0.0f);
    const GstStructure *params = getModelProcOutputInfo().get();
    if (!params)
        return mean_sizes;

    GValueArray *arr = nullptr;
    if (gst_structure_get_array(const_cast<GstStructure *>(params), "cls_mean_size", &arr) && arr) {
        if (arr->n_values == mean_sizes.size())
            for (guint i = 0; i < arr->n_values; ++i)
                mean_sizes[i] = static_cast<float>(g_value_get_double(g_value_array_get_nth(arr, i)));
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        g_value_array_free(arr);
        G_GNUC_END_IGNORE_DEPRECATIONS
    }
    return mean_sizes;
}

void Mono3DConverter::parseOutputs(const float *logits, const std::vector<size_t> &logits_dims, const float *boxes,
                                   const float *dim3d, const float *depth, const float *angle, const Calibration &calib,
                                   const std::vector<float> &cls_mean_size,
                                   std::vector<DetectedObject> &objects) const {
    const size_t num_queries = logits_dims[logits_dims.size() - 2];
    const size_t num_classes = logits_dims.back();
    if (num_queries == 0 || num_classes == 0)
        throw std::invalid_argument("mono3d: logits output has zero queries or classes.");

    const size_t topk = std::min(readTopK(), num_queries * num_classes);

    // Top-k over the flattened (query x class) sigmoid scores.
    std::vector<std::tuple<float, size_t, size_t>> scored; // (prob, query, class)
    scored.reserve(num_queries * num_classes);
    for (size_t q = 0; q < num_queries; ++q)
        for (size_t c = 0; c < num_classes; ++c)
            scored.emplace_back(sigmoid(logits[q * num_classes + c]), q, c);

    std::partial_sort(scored.begin(), scored.begin() + topk, scored.end(),
                      [](const auto &a, const auto &b) { return std::get<0>(a) > std::get<0>(b); });

    // Calibration scalars
    const double fu = calib.p2[0];
    const double cu = calib.p2[2];
    const double fv = calib.p2[5];
    const double cv = calib.p2[6];
    const double tx = (fu != 0.0) ? calib.p2[3] / (-fu) : 0.0;
    const double ty = (fv != 0.0) ? calib.p2[7] / (-fv) : 0.0;
    const double orig_w = calib.orig_width;
    const double orig_h = calib.orig_height;

    for (size_t i = 0; i < topk; ++i) {
        const float prob = std::get<0>(scored[i]);
        const size_t q = std::get<1>(scored[i]);
        const size_t cls = std::get<2>(scored[i]);

        // Threshold on the classification probability (pre-uncertainty), matching the reference.
        if (prob < confidence_threshold)
            continue;

        // 2D box (normalized cxcylrtb)
        const float *box = boxes + q * 6;
        const double bcx = box[0];
        const double bcy = box[1];
        const double bl = box[2];
        const double br = box[3];
        const double bt = box[4];
        const double bb = box[5];

        const double x1n = bcx - bl;
        const double y1n = bcy - bt;
        const double x2n = bcx + br;
        const double y2n = bcy + bb;

        // depth + uncertainty
        const double depth_val = depth[q * 2 + 0];
        const double sigma = std::exp(-depth[q * 2 + 1]);

        // 3D dimensions (h, w, l) + per-class mean size
        double h3d = dim3d[q * 3 + 0] + cls_mean_size[cls * 3 + 0];
        double w3d = dim3d[q * 3 + 1] + cls_mean_size[cls * 3 + 1];
        double l3d = dim3d[q * 3 + 2] + cls_mean_size[cls * 3 + 2];

        // 3D location: back-project the (cx, cy) anchor through the calibration.
        const double x3d = bcx * orig_w;
        const double y3d = bcy * orig_h;
        const double X = ((x3d - cu) * depth_val) / fu + tx;
        double Y = ((y3d - cv) * depth_val) / fv + ty;
        const double Z = depth_val;
        Y += h3d / 2.0; // move from 3D centre to bottom-centre (KITTI convention)

        // heading -> ry. Uses the 2D box centre x (in pixels), NOT the 3D anchor.
        const double alpha = getHeadingAngle(angle + q * 24);
        const double u_ry = ((x1n + x2n) / 2.0) * orig_w;
        double ry = alpha + std::atan2(u_ry - cu, fu);
        if (ry > PI)
            ry -= 2.0 * PI;
        if (ry < -PI)
            ry += 2.0 * PI;

        const std::string label = getLabels().empty() ? std::string() : getLabelByLabelId(cls);

        // 2D ROI stored normalized (top-left + size) relative to the full frame.
        DetectedObject object(x1n, y1n, x2n - x1n, y2n - y1n, 0.0, prob * sigma, cls, label, 1.0, 1.0, false);
        object.has_3d = true;
        object.loc_x = X;
        object.loc_y = Y;
        object.loc_z = Z;
        object.dim_h = h3d;
        object.dim_w = w3d;
        object.dim_l = l3d;
        object.rotation_y = ry;
        object.alpha = alpha;
        objects.push_back(std::move(object));
    }
}

TensorsTable Mono3DConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    try {
        InferenceBackend::OutputBlob::Ptr logits_blob, boxes_blob, dim_blob, depth_blob, angle_blob;

        // Resolve unambiguous outputs by last-dim, and logits/dim by name.
        for (const auto &it : output_blobs) {
            const auto &blob = it.second;
            if (!blob)
                continue;
            const auto &dims = blob->GetDims();
            if (dims.size() < 2)
                continue;
            const size_t last = dims.back();
            const std::string name = toLower(it.first);

            if (last == 6)
                boxes_blob = blob;
            else if (last == 2)
                depth_blob = blob;
            else if (last == 24)
                angle_blob = blob;
            else if (name.find("logit") != std::string::npos)
                logits_blob = blob;
            else if (name.find("dim") != std::string::npos || name.find("size") != std::string::npos)
                dim_blob = blob;
        }

        if (!logits_blob || !dim_blob)
            throw std::runtime_error(
                "mono3d: could not identify 'pred_logits' and 'pred_3d_dim' outputs. The exported model must assign "
                "distinct, recognizable output names (e.g. pred_logits, pred_boxes, pred_3d_dim, pred_depth, "
                "pred_angle); otherwise the two [B, Q, 3] outputs cannot be disambiguated.");
        if (!boxes_blob || !depth_blob || !angle_blob)
            throw std::runtime_error("mono3d: missing one of the required outputs (boxes[6]/depth[2]/angle[24]).");

        const auto &image_info = getModelInputImageInfo();
        const size_t batch_size = image_info.batch_size;

        const std::vector<size_t> logits_dims = logits_blob->GetDims();
        const size_t num_classes = logits_dims.back();

        const Calibration calib = readCalibration();
        const std::vector<float> cls_mean_size = readClassMeanSizes(num_classes);

        const std::vector<float> logits = blobToFloat(logits_blob);
        const std::vector<float> boxes = blobToFloat(boxes_blob);
        const std::vector<float> dim3d = blobToFloat(dim_blob);
        const std::vector<float> depth = blobToFloat(depth_blob);
        const std::vector<float> angle = blobToFloat(angle_blob);

        const size_t logits_stride = logits_blob->GetSize() / batch_size;
        const size_t boxes_stride = boxes_blob->GetSize() / batch_size;
        const size_t dim_stride = dim_blob->GetSize() / batch_size;
        const size_t depth_stride = depth_blob->GetSize() / batch_size;
        const size_t angle_stride = angle_blob->GetSize() / batch_size;

        DetectedObjectsTable objects_table(batch_size);
        for (size_t b = 0; b < batch_size; ++b)
            parseOutputs(logits.data() + b * logits_stride, logits_dims, boxes.data() + b * boxes_stride,
                         dim3d.data() + b * dim_stride, depth.data() + b * depth_stride,
                         angle.data() + b * angle_stride, calib, cls_mean_size, objects_table[b]);

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do mono3d post-processing."));
    }
    return TensorsTable{};
}
