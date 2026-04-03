/*******************************************************************************
 * Copyright (C) 2024-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "centerface.h"

#include "copy_blob_to_gststruct.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h>
#include <gst/gst.h>
#include <opencv2/opencv.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace post_processing;

void CenterfaceConverter::decode(const float *heatmap, int heatmap_height, int heatmap_width, const float *scale,
                                 const float *offset, const float *landmarks, std::vector<DetectedObject> &faces,
                                 float scoreThresh, size_t input_width, size_t input_height) const {

    int spacial_size = heatmap_width * heatmap_height;

    const float *scale0 = scale;
    const float *scale1 = scale0 + spacial_size;

    const float *offset0 = offset;
    const float *offset1 = offset0 + spacial_size;

    std::vector<int> ids;
    for (int i = 0; i < heatmap_height; i++) {
        for (int j = 0; j < heatmap_width; j++) {
            if (heatmap[i * heatmap_width + j] > scoreThresh) {
                ids.push_back(i);
                ids.push_back(j);
            }
        }
    }

    for (unsigned int i = 0; i < ids.size() / 2; i++) {
        int id_h = ids[2 * i];
        int id_w = ids[2 * i + 1];
        int index = id_h * heatmap_width + id_w;

        float s0 = std::exp(scale0[index]) * 4;
        float s1 = std::exp(scale1[index]) * 4;
        float o0 = offset0[index];
        float o1 = offset1[index];

        float x = std::min((float)std::max(0.0, (id_w + o1 + 0.5) * 4 - s1 / 2), (float)input_width);
        float y = std::min((float)std::max(0.0, (id_h + o0 + 0.5) * 4 - s0 / 2), (float)input_height);
        float w = std::min(x + s1, (float)input_width) - x;
        float h = std::min(y + s0, (float)input_height) - y;

        DetectedObject detected_object(x, y, w, h, 0, heatmap[index], 1, "", 1.0f / input_width, 1.0f / input_height,
                                       false);

        float scaled_landmarks[2 * NUMBER_OF_LANDMARK_POINTS];
        for (int j = 0; j < NUMBER_OF_LANDMARK_POINTS; j++) {
            scaled_landmarks[2 * j] = landmarks[(2 * j + 1) * spacial_size + index] * s1 / w;
            scaled_landmarks[2 * j + 1] = landmarks[(2 * j) * spacial_size + index] * s0 / h;
        }
        addLandmarksTensor(detected_object, scaled_landmarks, NUMBER_OF_LANDMARK_POINTS);
        faces.push_back(detected_object);
    }
}

const float *CenterfaceConverter::parseOutputBlob(const OutputBlobs &output_blobs, const std::string &key,
                                                  size_t batch_size, size_t batch_number) const {

    const InferenceBackend::OutputBlob::Ptr &blob = output_blobs.at(key);
    if (not blob)
        throw std::invalid_argument("Output blob is nullptr.");

    const std::vector<size_t> &dims = blob->GetDims();
    size_t dims_size = dims.size();

    if (dims_size < BlobToROIConverter::min_dims_size)
        throw std::invalid_argument("Output blob dimensions size " + std::to_string(dims_size) +
                                    " is not supported (less than " +
                                    std::to_string(BlobToROIConverter::min_dims_size) + ").");

    size_t unbatched_size = blob->GetSize() / batch_size;
    return reinterpret_cast<const float *>(blob->GetData()) + unbatched_size * batch_number;
}

void CenterfaceConverter::addLandmarksTensor(DetectedObject &detected_object, const float *landmarks,
                                             int num_of_landmarks) const {
    GstStructure *tensor = gst_structure_copy(getModelProcOutputInfo().get());
    gst_structure_set_name(tensor, "keypoints");
    gst_structure_set(tensor, "type", G_TYPE_STRING, "keypoints", NULL);
    gst_structure_set(tensor, "precision", G_TYPE_INT, GVA_PRECISION_FP32, NULL);
    gst_structure_set(tensor, "format", G_TYPE_STRING, GST_ANALYTICS_KEYPOINT_FACE_CENTERFACE_5, NULL);

    // set per-keypoint confidence as a vector (same value for all landmarks)
    {
        GValue gval = G_VALUE_INIT;
        GValue garr = G_VALUE_INIT;
        gst_value_array_init(&garr, num_of_landmarks);
        g_value_init(&gval, G_TYPE_FLOAT);
        for (int k = 0; k < num_of_landmarks; k++) {
            g_value_set_float(&gval, static_cast<float>(detected_object.confidence));
            gst_value_array_append_value(&garr, &gval);
        }
        g_value_unset(&gval);
        gst_structure_set_value(tensor, "confidence", &garr);
        g_value_unset(&garr);
    }

    GValueArray *data = g_value_array_new(2);
    GValue gvalue = G_VALUE_INIT;
    g_value_init(&gvalue, G_TYPE_UINT);
    g_value_set_uint(&gvalue, safe_convert<uint32_t>(num_of_landmarks));
    g_value_array_append(data, &gvalue);
    g_value_set_uint(&gvalue, 2);
    g_value_array_append(data, &gvalue);
    gst_structure_set_array(tensor, "dims", data);
    g_value_array_free(data);

    copy_buffer_to_structure(tensor, reinterpret_cast<const void *>(landmarks), 2 * num_of_landmarks * sizeof(float));

    detected_object.tensors.push_back(tensor);
}

TensorsTable CenterfaceConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        size_t batch_size = model_input_image_info.batch_size;

        size_t input_width = getModelInputImageInfo().width;
        size_t input_height = getModelInputImageInfo().height;

        DetectedObjectsTable objects_table(batch_size);

        for (size_t batch_number = 0; batch_number < batch_size; ++batch_number) {
            auto &objects = objects_table[batch_number];

            const float *heatmap;
            const float *scale;
            const float *offset;
            const float *landmarks;

            heatmap = parseOutputBlob(output_blobs, HEATMAP_KEY, batch_size, batch_number);
            scale = parseOutputBlob(output_blobs, SCALE_KEY, batch_size, batch_number);
            offset = parseOutputBlob(output_blobs, OFFSET_KEY, batch_size, batch_number);
            landmarks = parseOutputBlob(output_blobs, LANDMARKS_KEY, batch_size, batch_number);

            const std::vector<size_t> &heatmap_dims = output_blobs.at(HEATMAP_KEY)->GetDims();
            decode(heatmap, heatmap_dims[2], heatmap_dims[3], scale, offset, landmarks, objects, 0.5, input_width,
                   input_height);
        }

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do Centerface post-processing."));
    }
    return TensorsTable{};
}
