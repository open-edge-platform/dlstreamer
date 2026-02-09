/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "yolo_v26.h"

#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"

#include <gst/gst.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

using namespace post_processing;

TensorsTable YOLOv26ObbConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        size_t batch_size = model_input_image_info.batch_size;

        DetectedObjectsTable objects_table(batch_size);

        for (size_t batch_number = 0; batch_number < batch_size; ++batch_number) {
            auto &objects = objects_table[batch_number];

            for (const auto &blob_iter : output_blobs) {
                const InferenceBackend::OutputBlob::Ptr &blob = blob_iter.second;
                if (not blob)
                    throw std::invalid_argument("Output blob is nullptr.");

                size_t unbatched_size = blob->GetSize() / batch_size;
                parseOutputBlob(reinterpret_cast<const float *>(blob->GetData()) + unbatched_size * batch_number,
                                blob->GetDims(), objects, true);
            }
        }

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do yolo26-obb post-processing."));
    }
    return TensorsTable{};
}

TensorsTable YOLOv26PoseConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        size_t batch_size = model_input_image_info.batch_size;

        DetectedObjectsTable objects_table(batch_size);

        for (size_t batch_number = 0; batch_number < batch_size; ++batch_number) {
            auto &objects = objects_table[batch_number];

            for (const auto &blob_iter : output_blobs) {
                const InferenceBackend::OutputBlob::Ptr &blob = blob_iter.second;
                if (not blob)
                    throw std::invalid_argument("Output blob is nullptr.");

                size_t unbatched_size = blob->GetSize() / batch_size;
                parseOutputBlob(reinterpret_cast<const float *>(blob->GetData()) + unbatched_size * batch_number,
                                blob->GetDims(), objects);
            }
        }

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do yolo26-pose post-processing."));
    }
    return TensorsTable{};
}

static const std::vector<std::string> point_names = {
    "nose",    "eye_l",   "eye_r", "ear_l", "ear_r",  "shoulder_l", "shoulder_r", "elbow_l", "elbow_r",
    "wrist_l", "wrist_r", "hip_l", "hip_r", "knee_l", "knee_r",     "ankle_l",    "ankle_r"};
static const std::vector<std::string> point_connections = {
    "nose",    "eye_l", "nose",       "eye_r",      "ear_l",      "shoulder_l", "ear_r",   "shoulder_r", "eye_l",
    "ear_l",   "eye_r", "ear_r",      "shoulder_l", "shoulder_r", "shoulder_l", "hip_l",   "shoulder_r", "hip_r",
    "hip_l",   "hip_r", "shoulder_l", "elbow_l",    "shoulder_r", "elbow_r",    "elbow_l", "wrist_l",    "elbow_r",
    "wrist_r", "hip_l", "knee_l",     "hip_r",      "knee_r",     "knee_l",     "ankle_l", "knee_r",     "ankle_r"};

void YOLOv26PoseConverter::parseOutputBlob(const float *data, const std::vector<size_t> &dims,
                                           std::vector<DetectedObject> &objects) const {
    size_t boxes_dims_size = dims.size();
    size_t input_width = getModelInputImageInfo().width;
    size_t input_height = getModelInputImageInfo().height;

    size_t object_size = dims[boxes_dims_size - 1];
    size_t max_proposal_count = dims[boxes_dims_size - 2];
    size_t keypoint_count = (object_size - YOLOV26_OFFSET_L - 1) / 3;

    // iterate through all bounding-box predictions
    float *output_data = (float *)data;
    for (size_t i = 0; i < max_proposal_count; ++i) {

        float confidence = output_data[YOLOV26_OFFSET_BS];
        if (confidence > confidence_threshold) {

            // coordinates are absolute corners of a bounding box
            float x = output_data[YOLOV26_OFFSET_X1];
            float y = output_data[YOLOV26_OFFSET_Y1];
            float w = output_data[YOLOV26_OFFSET_X2] - x;
            float h = output_data[YOLOV26_OFFSET_Y2] - y;
            float labelId = output_data[YOLOV26_OFFSET_L];

            auto detected_object =
                DetectedObject(x, y, w, h, 0, confidence, labelId, BlobToMetaConverter::getLabelByLabelId(labelId),
                               1.0f / input_width, 1.0f / input_height, false);

            // create relative keypoint positions within bounding box
            cv::Mat positions(keypoint_count, 2, CV_32F);
            std::vector<float> confidences(keypoint_count, 0.0f);
            for (size_t k = 0; k < keypoint_count; k++) {
                float position_x = output_data[YOLOV26_OFFSET_L + 1 + k * 3 + 0];
                float position_y = output_data[YOLOV26_OFFSET_L + 1 + k * 3 + 1];
                positions.at<float>(k, 0) = (position_x - x) / w;
                positions.at<float>(k, 1) = (position_y - y) / h;
                confidences[k] = output_data[YOLOV26_OFFSET_L + 1 + k * 3 + 2];
            }

            // create tensor with keypoints
            GstStructure *gst_structure = gst_structure_copy(getModelProcOutputInfo().get());
            GVA::Tensor tensor(gst_structure);

            tensor.set_name("keypoints");
            tensor.set_format("keypoints");

            // set tensor data (positions)
            tensor.set_dims({static_cast<uint32_t>(keypoint_count), 2});
            tensor.set_data(reinterpret_cast<const void *>(positions.data), keypoint_count * 2 * sizeof(float));
            tensor.set_precision(GVA::Tensor::Precision::FP32);

            // set additional tensor properties as vectors: confidence, point names and point connections
            tensor.set_vector<float>("confidence", confidences);
            tensor.set_vector<std::string>("point_names", point_names);
            tensor.set_vector<std::string>("point_connections", point_connections);

            detected_object.tensors.push_back(tensor.gst_structure());
            objects.push_back(detected_object);
        }
        output_data += object_size;
    }
}

void YOLOv26SegConverter::parseOutputBlob(const float *boxes_data, const std::vector<size_t> &boxes_dims,
                                          const float *masks_data, const std::vector<size_t> &masks_dims,
                                          std::vector<DetectedObject> &objects) const {
    size_t boxes_dims_size = boxes_dims.size();
    size_t masks_dims_size = masks_dims.size();
    size_t input_width = getModelInputImageInfo().width;
    size_t input_height = getModelInputImageInfo().height;

    size_t object_size = boxes_dims[boxes_dims_size - 1];
    size_t max_proposal_count = boxes_dims[boxes_dims_size - 2];
    size_t mask_count = masks_dims[masks_dims_size - 3];
    size_t mask_height = masks_dims[masks_dims_size - 2];
    size_t mask_width = masks_dims[masks_dims_size - 1];

    // There is a common mask for all predictions, map it to OpenCV Mat object
    cv::Mat masks(mask_count, mask_width * mask_height, CV_32F, (float *)masks_data);

    // iterate through bounding-box predictions
    float *output_data = (float *)boxes_data;
    for (size_t i = 0; i < max_proposal_count; ++i) {

        float confidence = output_data[YOLOV26_OFFSET_BS];
        if (confidence > confidence_threshold) {

            // coordinates are absolute corners of a bounding box
            float x = output_data[YOLOV26_OFFSET_X1];
            float y = output_data[YOLOV26_OFFSET_Y1];
            float w = output_data[YOLOV26_OFFSET_X2] - x;
            float h = output_data[YOLOV26_OFFSET_Y2] - y;
            float labelId = output_data[YOLOV26_OFFSET_L];

            auto detected_object =
                DetectedObject(x, y, w, h, 0, confidence, labelId, BlobToMetaConverter::getLabelByLabelId(labelId),
                               1.0f / input_width, 1.0f / input_height, false);

            cv::Mat mask_scores(1, mask_count, CV_32F, output_data + YOLOV26_OFFSET_L + 1);

            // compose mask for detected bounding box
            cv::Mat composed_mask = mask_scores * masks;
            composed_mask = composed_mask.reshape(1, mask_height);

            // crop composed mask to fit into object bounding box
            cv::Mat cropped_mask;
            int cx = std::max(int(x * mask_width / input_width), int(0));
            int cy = std::max(int(y * mask_height / input_height), int(0));
            int cw = std::min(int(w * mask_width / input_width), int(mask_width - cx));
            int ch = std::min(int(h * mask_height / input_height), int(mask_height - cy));
            composed_mask(cv::Rect(cx, cy, cw, ch)).copyTo(cropped_mask);

            // apply sigmoid activation
            cropped_mask.forEach<float>([](float &element, const int position[]) -> void {
                std::ignore = position;
                element = 1 / (1 + std::exp(-element));
            });

            // create segmentation mask tensor
            GstStructure *gst_structure = gst_structure_copy(getModelProcOutputInfo().get());
            GVA::Tensor tensor(gst_structure);
            tensor.set_name("mask_yolo26");
            tensor.set_format("segmentation_mask");

            // set tensor data
            tensor.set_dims({safe_convert<uint32_t>(cropped_mask.cols), safe_convert<uint32_t>(cropped_mask.rows)});
            tensor.set_precision(GVA::Tensor::Precision::FP32);
            tensor.set_data(reinterpret_cast<const void *>(cropped_mask.data),
                            cropped_mask.rows * cropped_mask.cols * sizeof(float));

            // add tensor to the list of detected objects
            detected_object.tensors.push_back(tensor.gst_structure());
            objects.push_back(detected_object);
        }
        output_data += object_size;
    }
}

TensorsTable YOLOv26SegConverter::convert(const OutputBlobs &output_blobs) {
    ITT_TASK(__FUNCTION__);
    try {
        const auto &model_input_image_info = getModelInputImageInfo();
        size_t batch_size = model_input_image_info.batch_size;

        InferenceBackend::OutputBlob::Ptr boxes_blob = nullptr;
        InferenceBackend::OutputBlob::Ptr masks_blob = nullptr;

        for (const auto &blob_iter : output_blobs) {
            const InferenceBackend::OutputBlob::Ptr blob = blob_iter.second;
            std::vector<size_t> dims = blob->GetDims();
            // mask blob has shape: [batch, mask_count, height/4, width/4]
            if ((dims.size() == 4) && (dims[0] == batch_size) && (dims[2] == getModelInputImageInfo().height / 4) &&
                (dims[3] == getModelInputImageInfo().width / 4)) {
                masks_blob = InferenceBackend::OutputBlob::Ptr(blob);
            }
            // boxes blob has shape: [batch, num_boxes, 6 + mask_count] where default mask_count=32
            if ((dims.size() == 3) && (dims[0] == batch_size)) {
                boxes_blob = InferenceBackend::OutputBlob::Ptr(blob);
            }
        }

        if (boxes_blob == nullptr || masks_blob == nullptr) {
            std::throw_with_nested(std::runtime_error("Failed to intentify output blobs for yolo26-seg converter."));
        }

        DetectedObjectsTable objects_table(batch_size);

        for (size_t batch_number = 0; batch_number < batch_size; ++batch_number) {
            auto &objects = objects_table[batch_number];

            size_t boxes_unbatched_size = boxes_blob->GetSize() / batch_size;
            size_t masks_unbatched_size = masks_blob->GetSize() / batch_size;
            parseOutputBlob(
                reinterpret_cast<const float *>(boxes_blob->GetData()) + boxes_unbatched_size * batch_number,
                boxes_blob->GetDims(),
                reinterpret_cast<const float *>(masks_blob->GetData()) + masks_unbatched_size * batch_number,
                masks_blob->GetDims(), objects);
        }

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do yolo26-seg post-processing."));
    }
    return TensorsTable{};
}
