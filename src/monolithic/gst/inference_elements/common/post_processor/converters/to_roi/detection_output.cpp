/*******************************************************************************
 * Copyright (C) 2021-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "detection_output.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace post_processing;

#pragma pack(push, 1)
struct DetectionBlobElement {
    float imageId;
    float labelId;
    float confidence;
    float bbox_x_min;
    float bbox_y_min;
    float bbox_x_max;
    float bbox_y_max;
};
#pragma pack(pop)

void DetectionOutputConverter::parseOutputBlob(const InferenceBackend::OutputBlob::Ptr &blob,
                                               DetectedObjectsTable &objects, double roi_scale) const {

    if (!blob)
        throw std::invalid_argument("Output blob is nullptr.");

    if (!blob->GetData())
        throw std::runtime_error("Output blob data is nullptr.");

    if (blob->GetPrecision() != InferenceBackend::Blob::Precision::FP32)
        throw std::runtime_error("Unsupported label precision.");

    const float *data = reinterpret_cast<const float *>(blob->GetData());
    auto dims = blob->GetDims();
    size_t dims_size = dims.size();

    if (dims_size < BlobToROIConverter::min_dims_size)
        throw std::invalid_argument("Output blob dimensions size " + std::to_string(dims_size) +
                                    " is not supported (less than " +
                                    std::to_string(BlobToROIConverter::min_dims_size) + ").");

    for (size_t i = BlobToROIConverter::min_dims_size + 1; i < dims_size; ++i) {
        if (dims[dims_size - i] != 1)
            throw std::invalid_argument("All output blob dimensions, except for object size and max "
                                        "objects count, must be equal to 1");
    }

    size_t object_size = dims[dims_size - 1];
    if (object_size != DetectionOutputConverter::model_object_size)
        throw std::invalid_argument("Object size dimension of output blob is set to " + std::to_string(object_size) +
                                    ", but only " + std::to_string(DetectionOutputConverter::model_object_size) +
                                    " supported.");

    size_t max_proposal_count = dims[dims_size - 2];
    for (size_t i = 0; i < max_proposal_count; ++i) {
        auto *blobElement = reinterpret_cast<const DetectionBlobElement *>(&data[i * object_size]);
        int image_id = safe_convert<int>(blobElement->imageId);
        /* check if 'image_id' contains a valid index for 'frames' vector */
        if (image_id < 0 || (size_t)image_id >= objects.size()) {
            break;
        }

        /* discard inference results that do not match 'confidence_threshold' */
        if (blobElement->confidence < confidence_threshold) {
            continue;
        }

        float bbox_x = blobElement->bbox_x_min;
        float bbox_y = blobElement->bbox_y_min;
        float bbox_w = blobElement->bbox_x_max - blobElement->bbox_x_min;
        float bbox_h = blobElement->bbox_y_max - blobElement->bbox_y_min;

        // discard inference results that are invalid
        if (blobElement->bbox_x_min > 1.0 // x_min beyond max image boundary
            || blobElement->bbox_y_min > 1.0 // y_min beyond max image boundary
            || blobElement->bbox_x_max < 0.0 // x_max beyond min image boundary
            || blobElement->bbox_y_max < 0.0 // y_max beyond min image boundary
            || bbox_w == 0.0 // invalid width
            || bbox_y == 0.0 // invalid height
            ) {
            continue;
        }

        // TODO: in future we must return to use GVA::VideoFrame
        // GVA::VideoFrame video_frame(frames[image_id]->buffer, frames[image_id]->info);

        // TODO: check if we can simplify below code further
        // apply roi_scale if set
        if (roi_scale > 0 && roi_scale != 1) {
            bbox_x = bbox_x + bbox_w / 2 * (1 - roi_scale);
            bbox_y = bbox_y + bbox_h / 2 * (1 - roi_scale);
            bbox_w = bbox_w * roi_scale;
            bbox_h = bbox_h * roi_scale;
        }

        size_t label_id = safe_convert<size_t>(blobElement->labelId);
        objects[image_id].push_back(DetectedObject(bbox_x, bbox_y, bbox_w, bbox_h, 0, blobElement->confidence, label_id,
                                                   getLabelByLabelId(label_id)));
    }
}

TensorsTable DetectionOutputConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        DetectedObjectsTable objects(model_input_image_info.batch_size);

        const auto &detection_result = getModelProcOutputInfo();

        if (detection_result == nullptr)
            throw std::invalid_argument("detection_result tensor is nullptr");
        double roi_scale = 1.0;
        gst_structure_get_double(detection_result.get(), "roi_scale", &roi_scale);

        // Check whether we can handle this blob instead iterator
        for (const auto &blob_iter : output_blobs) {
            const InferenceBackend::OutputBlob::Ptr &blob = blob_iter.second;
            if (not blob)
                throw std::invalid_argument("Output blob is nullptr");

            parseOutputBlob(blob, objects, roi_scale);
        }

        return storeObjects(objects);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do SSD post-processing"));
    }
}
