/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>

#include "gst/analytics/analytics.h"
#include "pre_processor_info_parser.hpp"
#include "pre_processors.h"
#include "region_of_interest.h"
#include "tensor.h"
#include "utils.h"

using namespace InferenceBackend;

namespace {

InputPreprocessingFunction createImageInfoFunction(const GstStructure *params, const ImageInference::Ptr &inference) {
    double scale = 1.0;
    gst_structure_get_double(params, "scale", &scale);

    size_t width = 0;
    size_t height = 0;
    size_t batch = 0;
    int format = 0;
    int memory_type = 0;
    inference->GetModelImageInputInfo(width, height, batch, format, memory_type);

    return [width, height, scale](const InputBlob::Ptr &blob) {
        auto dims = blob->GetDims();
        float *data = reinterpret_cast<float *>(blob->GetData());
        data[0] = static_cast<float>(height);
        data[1] = static_cast<float>(width);
        std::fill(data + 2, data + dims[1], static_cast<float>(scale));
    };
}

InputPreprocessingFunction createSequenceIndexFunction() {
    return [](const InputBlob::Ptr &blob) {
        size_t maxSequenceSizePerPlate = blob->GetDims()[0];
        float *blob_data = reinterpret_cast<float *>(blob->GetData());
        std::fill(blob_data, blob_data + maxSequenceSizePerPlate, 1.0f);
    };
}

cv::Mat getTransform(cv::Mat *src, cv::Mat *dst) {
    if (not src or not dst)
        throw std::invalid_argument("Invalid cv::Mat inputs for GetTransform");
    cv::Mat col_mean_src;
    reduce(*src, col_mean_src, 0, cv::REDUCE_AVG);
    for (int i = 0; i < src->rows; i++) {
        src->row(i) -= col_mean_src;
    }

    cv::Mat col_mean_dst;
    reduce(*dst, col_mean_dst, 0, cv::REDUCE_AVG);
    for (int i = 0; i < dst->rows; i++) {
        dst->row(i) -= col_mean_dst;
    }

    cv::Scalar mean, dev_src, dev_dst;
    cv::meanStdDev(*src, mean, dev_src);
    dev_src(0) = std::max(static_cast<double>(std::numeric_limits<float>::epsilon()), dev_src(0));
    *src /= dev_src(0);
    cv::meanStdDev(*dst, mean, dev_dst);
    dev_dst(0) = std::max(static_cast<double>(std::numeric_limits<float>::epsilon()), dev_dst(0));
    *dst /= dev_dst(0);

    cv::Mat w, u, vt;
    cv::SVD::compute((*src).t() * (*dst), w, u, vt);
    cv::Mat r = (u * vt).t();
    cv::Mat m(2, 3, CV_32F);
    m.colRange(0, 2) = r * (dev_dst(0) / dev_src(0));
    m.col(2) = (col_mean_dst.t() - m.colRange(0, 2) * col_mean_src.t());
    return m;
}

void alignRgbImage(Image &image, const std::vector<float> &landmarks_points,
                   const std::vector<float> &reference_points) {
    cv::Mat ref_landmarks = cv::Mat(reference_points.size() / 2, 2, CV_32F);
    cv::Mat landmarks =
        cv::Mat(landmarks_points.size() / 2, 2, CV_32F, const_cast<float *>(&landmarks_points.front())).clone();
    for (int i = 0; i < ref_landmarks.rows; i++) {
        ref_landmarks.at<float>(i, 0) = reference_points[2 * i] * image.width;
        ref_landmarks.at<float>(i, 1) = reference_points[2 * i + 1] * image.height;
        landmarks.at<float>(i, 0) *= image.width;
        landmarks.at<float>(i, 1) *= image.height;
    }
    cv::Mat m = getTransform(&ref_landmarks, &landmarks);
    for (int plane_num = 0; plane_num < 4; plane_num++) {
        if (image.planes[plane_num]) {
            cv::Mat mat0(image.height, image.width, CV_8UC1, image.planes[plane_num], image.stride[plane_num]);
            cv::warpAffine(mat0, mat0, m, mat0.size(), cv::WARP_INVERSE_MAP);
        }
    }
}

Image getImage(const InputBlob::Ptr &blob) {
    Image image = Image();

    auto dims = blob->GetDims();
    auto layout = blob->GetLayout();
    switch (layout) {
    case InputBlob::Layout::NCHW:
        image.height = dims[2];
        image.width = dims[3];
        break;
    case InputBlob::Layout::NHWC:
        image.height = dims[1];
        image.width = dims[2];
        break;
    default:
        throw std::invalid_argument("Unsupported layout for image");
    }
    uint8_t *data = reinterpret_cast<uint8_t *>(blob->GetData());
    data += blob->GetIndexInBatch() * 3 * image.width * image.height;
    image.planes[0] = data;
    image.planes[1] = data + image.width * image.height;
    image.planes[2] = data + 2 * image.width * image.height;
    image.planes[3] = nullptr;

    image.stride[0] = image.stride[1] = image.stride[2] = image.stride[3] = image.width;

    image.format = FourCC::FOURCC_RGBP;
    return image;
}

InputPreprocessingFunction createFaceAlignmentFunction(GstStructure *params, GstVideoRegionOfInterestMeta *roi_meta) {
    std::vector<float> reference_points;
    std::vector<float> landmarks_points;
    // look for tensor data with corresponding format
    for (GList *l = roi_meta->params; l; l = g_list_next(l)) {
        GstStructure *s = GST_STRUCTURE(l->data);
        GVA::Tensor tensor(s);
        if (tensor.format() == "landmark_points") {
            landmarks_points = tensor.data<float>();
            break;
        }
    }
    // load reference points from JSON input_preproc description
    GValueArray *alignment_points = nullptr;
    if (params and gst_structure_get_array(params, "alignment_points", &alignment_points)) {
        for (size_t i = 0; i < alignment_points->n_values; i++) {
            reference_points.push_back(g_value_get_double(alignment_points->values + i));
        }
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        g_value_array_free(alignment_points);
        G_GNUC_END_IGNORE_DEPRECATIONS
    }

    if (landmarks_points.size() and landmarks_points.size() == reference_points.size()) {
        return [reference_points, landmarks_points](const InputBlob::Ptr &blob) {
            Image image = getImage(blob);
            alignRgbImage(image, landmarks_points, reference_points);
        };
    }
    return [](const InputBlob::Ptr &) {};
}

InputPreprocessingFunction createImageInputFunction(GstStructure *params, GstVideoRegionOfInterestMeta *roi) {
    return createFaceAlignmentFunction(params, roi);
}

InputPreprocessingFunction getInputPreprocFunctrByLayerType(const std::string &format,
                                                            const ImageInference::Ptr &inference,
                                                            GstStructure *preproc_params,
                                                            GstVideoRegionOfInterestMeta *roi) {
    InputPreprocessingFunction result;
    if (format == "sequence_index")
        result = createSequenceIndexFunction();
    else if (format == "image_info")
        result = createImageInfoFunction(preproc_params, inference);
    else
        result = createImageInputFunction(preproc_params, roi);

    return result;
}

} // anonymous namespace

std::map<std::string, InputLayerDesc::Ptr>
GetInputPreprocessors(const std::shared_ptr<InferenceBackend::ImageInference> &inference,
                      const std::vector<ModelInputProcessorInfo::Ptr> &model_input_processor_info,
                      GstVideoRegionOfInterestMeta *roi) {
    // ITT_TASK(__FUNCTION__);
    std::map<std::string, InferenceBackend::InputLayerDesc::Ptr> preprocessors;
    for (const ModelInputProcessorInfo::Ptr &preproc : model_input_processor_info) {
        preprocessors[preproc->format] = std::make_shared<InputLayerDesc>(InputLayerDesc());
        preprocessors[preproc->format]->name = preproc->layer_name;
        preprocessors[preproc->format]->preprocessor =
            getInputPreprocFunctrByLayerType(preproc->format, inference, preproc->params, roi);

        preprocessors[preproc->format]->input_image_preroc_params =
            (preproc->format == "image") ? PreProcParamsParser(preproc->params).parse() : nullptr;
    }
    return preprocessors;
}

InputPreprocessorsFactory GET_INPUT_PREPROCESSORS = GetInputPreprocessors;
