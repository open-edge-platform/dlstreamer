/*******************************************************************************
 * Copyright (C) 2024-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "yolo_v8.h"

#include "copy_blob_to_gststruct.h"
#include "inference_backend/image_inference.h"
#include "inference_backend/logger.h"
#include "safe_arithmetic.hpp"

#include <dlstreamer/gst/metadata/gstanalyticskeypointdescriptor.h>
#include <dlstreamer/gst/videoanalytics/tensor.h>
#include <gst/gst.h>

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace post_processing;

namespace {

/*
 * YOLOv8 output blobs are channel-major: [object_size, num_proposals]. Instead of transposing the whole blob,
 * the class score rows are scanned contiguously (SIMD friendly) to find the best score of each proposal, and the
 * class index is resolved only for proposals passing the threshold. Scores, class ids and proposal order match
 * cv::minMaxLoc run on each row of the transposed blob: NaN scores are ignored and ties go to the lowest class id.
 */
template <typename Callback>
void forEachCandidate(const float *data, size_t num_proposals, size_t class_offset, size_t class_count,
                      double threshold, Callback &&on_candidate) {
    constexpr size_t block_size = 512;
    float max_scores[block_size];
    const float *class_rows = data + class_offset * num_proposals;

    for (size_t block_start = 0; block_start < num_proposals; block_start += block_size) {
        const size_t block_len = std::min(block_size, num_proposals - block_start);

        std::fill_n(max_scores, block_len, -std::numeric_limits<float>::infinity());
        for (size_t c = 0; c < class_count; ++c) {
            const float *row = class_rows + c * num_proposals + block_start;
            for (size_t j = 0; j < block_len; ++j)
                max_scores[j] = row[j] > max_scores[j] ? row[j] : max_scores[j];
        }

        for (size_t j = 0; j < block_len; ++j) {
            const float max_score = max_scores[j];
            if (max_score > threshold) {
                const size_t i = block_start + j;
                size_t class_id = 0;
                while (class_rows[class_id * num_proposals + i] != max_score)
                    ++class_id;
                on_candidate(i, max_score, class_id);
            }
        }
    }
}

} // namespace

void YOLOv8Converter::parseOutputBlob(const float *data, const std::vector<size_t> &dims,
                                      std::vector<DetectedObject> &objects, bool oob) const {

    size_t dims_size = dims.size();
    size_t input_width = getModelInputImageInfo().width;
    size_t input_height = getModelInputImageInfo().height;

    if (dims_size < BlobToROIConverter::min_dims_size)
        throw std::invalid_argument("Output blob dimensions size " + std::to_string(dims_size) +
                                    " is not supported (less than " +
                                    std::to_string(BlobToROIConverter::min_dims_size) + ").");

    size_t object_size = dims[dims_size - 2];
    size_t max_proposal_count = dims[dims_size - 1];
    size_t non_class_fields = YOLOV8_OFFSET_CS + (oob ? 1 : 0);

    if (object_size < non_class_fields)
        throw std::invalid_argument("Output blob object size " + std::to_string(object_size) +
                                    " is not supported (less than " + std::to_string(non_class_fields) + ").");

    // blob layout is [object_size, max_proposal_count]: field f of proposal i is at data[f * max_proposal_count + i]
    auto field = [&](size_t f, size_t i) { return data[f * max_proposal_count + i]; };

    forEachCandidate(data, max_proposal_count, YOLOV8_OFFSET_CS, object_size - non_class_fields, confidence_threshold,
                     [&](size_t i, float max_class_score, size_t class_id) {
                         float x = field(YOLOV8_OFFSET_X, i);
                         float y = field(YOLOV8_OFFSET_Y, i);
                         float w = field(YOLOV8_OFFSET_W, i);
                         float h = field(YOLOV8_OFFSET_H, i);
                         float r = oob ? field(object_size - 1, i) : 0;
                         objects.push_back(DetectedObject(x, y, w, h, r, max_class_score, class_id,
                                                          BlobToMetaConverter::getLabelByLabelId(class_id),
                                                          1.0f / input_width, 1.0f / input_height, true));
                     });
}

TensorsTable YOLOv8Converter::convert(const OutputBlobs &output_blobs) {
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
                                blob->GetDims(), objects, false);
            }
        }

        return storeObjects(objects_table);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to do YoloV8 post-processing."));
    }
    return TensorsTable{};
}

TensorsTable YOLOv8ObbConverter::convert(const OutputBlobs &output_blobs) {
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
        std::throw_with_nested(std::runtime_error("Failed to do YoloV8-OBB post-processing."));
    }
    return TensorsTable{};
}

TensorsTable YOLOv8PoseConverter::convert(const OutputBlobs &output_blobs) {
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
        std::throw_with_nested(std::runtime_error("Failed to do YoloV8 post-processing."));
    }
    return TensorsTable{};
}

static const GstAnalyticsKeypointDescriptor *coco17_descriptor =
    gst_analytics_keypoint_descriptor_lookup(GST_ANALYTICS_KEYPOINT_BODY_POSE_COCO_17);

void YOLOv8PoseConverter::parseOutputBlob(const float *data, const std::vector<size_t> &dims,
                                          std::vector<DetectedObject> &objects) const {
    size_t boxes_dims_size = dims.size();
    size_t input_width = getModelInputImageInfo().width;
    size_t input_height = getModelInputImageInfo().height;

    if (boxes_dims_size < BlobToROIConverter::min_dims_size)
        throw std::invalid_argument("Output blob dimensions size " + std::to_string(boxes_dims_size) +
                                    " is not supported (less than " +
                                    std::to_string(BlobToROIConverter::min_dims_size) + ").");

    size_t object_size = dims[boxes_dims_size - 2];
    size_t max_proposal_count = dims[boxes_dims_size - 1];
    size_t keypoint_count = (object_size - YOLOV8_OFFSET_CS - 1) / 3;

    // blob layout is [object_size, max_proposal_count]: field f of proposal i is at data[f * max_proposal_count + i]
    auto field = [&](size_t f, size_t i) { return data[f * max_proposal_count + i]; };
    const float *confidence_row = data + YOLOV8_OFFSET_CS * max_proposal_count;

    const std::vector<std::string> names(coco17_descriptor->point_names,
                                         coco17_descriptor->point_names + coco17_descriptor->point_count);
    const std::vector<uint32_t> connections(coco17_descriptor->skeleton_connections,
                                            coco17_descriptor->skeleton_connections +
                                                coco17_descriptor->skeleton_connection_count * 2);

    for (size_t i = 0; i < max_proposal_count; ++i) {
        float confidence = confidence_row[i];
        if (confidence > confidence_threshold) {

            // coordinates are relative to bounding box center
            float w = field(YOLOV8_OFFSET_W, i);
            float h = field(YOLOV8_OFFSET_H, i);
            float x = field(YOLOV8_OFFSET_X, i) - w / 2;
            float y = field(YOLOV8_OFFSET_Y, i) - h / 2;

            auto detected_object =
                DetectedObject(x, y, w, h, 0, confidence, 0, BlobToMetaConverter::getLabelByLabelId(0),
                               1.0f / input_width, 1.0f / input_height, false);

            // create relative keypoint positions within bounding box
            cv::Mat positions(keypoint_count, 2, CV_32F);
            std::vector<float> confidences(keypoint_count, 0.0f);
            for (size_t k = 0; k < keypoint_count; k++) {
                float position_x = field(YOLOV8_OFFSET_CS + 1 + k * 3 + 0, i);
                float position_y = field(YOLOV8_OFFSET_CS + 1 + k * 3 + 1, i);
                positions.at<float>(k, 0) = (position_x - x) / w;
                positions.at<float>(k, 1) = (position_y - y) / h;
                confidences[k] = field(YOLOV8_OFFSET_CS + 1 + k * 3 + 2, i);
            }

            // create tensor with keypoints
            GstStructure *gst_structure = gst_structure_copy(getModelProcOutputInfo().get());
            GVA::Tensor tensor(gst_structure);

            tensor.set_name(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
            tensor.set_type(GVA::GST_ANALYTICS_KEYPOINTS_2_TENSOR);
            tensor.set_format(coco17_descriptor->semantic_tag);

            // set tensor data (positions)
            tensor.set_dims({static_cast<uint32_t>(keypoint_count), 2});
            tensor.set_data(reinterpret_cast<const void *>(positions.data), keypoint_count * 2 * sizeof(float));
            tensor.set_precision(GVA::Tensor::Precision::FP32);

            // set additional tensor properties as vectors: confidence, point names and point connections
            tensor.set_vector<float>("confidence", confidences);
            tensor.set_vector<std::string>("point_names", names);
            tensor.set_vector<uint32_t>("point_connections", connections);

            detected_object.tensors.push_back(tensor.gst_structure());
            objects.push_back(detected_object);
        }
    }
}

void YOLOv8SegConverter::parseOutputBlob(const float *boxes_data, const std::vector<size_t> &boxes_dims,
                                         const float *masks_data, const std::vector<size_t> &masks_dims,
                                         std::vector<DetectedObject> &objects) const {
    size_t boxes_dims_size = boxes_dims.size();
    size_t masks_dims_size = masks_dims.size();
    size_t input_width = getModelInputImageInfo().width;
    size_t input_height = getModelInputImageInfo().height;

    if (boxes_dims_size < BlobToROIConverter::min_dims_size)
        throw std::invalid_argument("Output blob dimensions size " + std::to_string(boxes_dims_size) +
                                    " is not supported (less than " +
                                    std::to_string(BlobToROIConverter::min_dims_size) + ").");

    size_t object_size = boxes_dims[boxes_dims_size - 2];
    size_t max_proposal_count = boxes_dims[boxes_dims_size - 1];
    size_t mask_count = masks_dims[masks_dims_size - 3];
    size_t mask_height = masks_dims[masks_dims_size - 2];
    size_t mask_width = masks_dims[masks_dims_size - 1];

    if (object_size < YOLOV8_OFFSET_CS + mask_count)
        throw std::invalid_argument("Output blob object size " + std::to_string(object_size) +
                                    " is not supported (less than " + std::to_string(YOLOV8_OFFSET_CS + mask_count) +
                                    ").");
    size_t class_count = object_size - mask_count - YOLOV8_OFFSET_CS;

    // blob layout is [object_size, max_proposal_count]: field f of proposal i is at data[f * max_proposal_count + i]
    auto field = [&](size_t f, size_t i) { return boxes_data[f * max_proposal_count + i]; };

    // Map masks
    cv::Mat masks(mask_count, mask_width * mask_height, CV_32F, (float *)masks_data);
    cv::Mat mask_scores(1, mask_count, CV_32F);

    forEachCandidate(
        boxes_data, max_proposal_count, YOLOV8_OFFSET_CS, class_count, confidence_threshold,
        [&](size_t i, float max_class_score, size_t class_id) {
            // coordinates are relative to bounding box center
            float w = field(YOLOV8_OFFSET_W, i);
            float h = field(YOLOV8_OFFSET_H, i);
            float x = field(YOLOV8_OFFSET_X, i) - w / 2;
            float y = field(YOLOV8_OFFSET_Y, i) - h / 2;

            auto detected_object = DetectedObject(x, y, w, h, 0, max_class_score, class_id,
                                                  BlobToMetaConverter::getLabelByLabelId(class_id), 1.0f / input_width,
                                                  1.0f / input_height, false);

            // compose mask for detected bounding box
            for (size_t m = 0; m < mask_count; ++m)
                mask_scores.at<float>(0, static_cast<int>(m)) = field(YOLOV8_OFFSET_CS + class_count + m, i);
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
                (void)position;
                element = 1 / (1 + std::exp(-element));
            });

            // create segmentation mask tensor
            GstStructure *gst_structure = gst_structure_copy(getModelProcOutputInfo().get());
            GVA::Tensor tensor(gst_structure);
            tensor.set_name("mask_yolov8");
            tensor.set_format(GVA::TENSOR_FORMAT_INSTANCE_SEGMENTATION);
            tensor.set_type(GVA::GST_ANALYTICS_SEGMENTATION_2_TENSOR);

            // set tensor data
            tensor.set_dims({safe_convert<uint32_t>(cropped_mask.cols), safe_convert<uint32_t>(cropped_mask.rows)});
            tensor.set_precision(GVA::Tensor::Precision::FP32);
            tensor.set_data(reinterpret_cast<const void *>(cropped_mask.data),
                            cropped_mask.rows * cropped_mask.cols * sizeof(float));

            // add tensor to the list of detected objects
            detected_object.tensors.push_back(tensor.gst_structure());
            objects.push_back(detected_object);

            // Future optimization: generate masks after running NMS algorithm on detected objects
        });
}

TensorsTable YOLOv8SegConverter::convert(const OutputBlobs &output_blobs) {
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
            // boxes blob has shape: [batch, 5 + class_count + mask_count, num_boxes]
            if ((dims.size() == 3) && (dims[0] == batch_size)) {
                boxes_blob = InferenceBackend::OutputBlob::Ptr(blob);
            }
        }

        if (boxes_blob == nullptr || masks_blob == nullptr) {
            std::throw_with_nested(std::runtime_error("Failed to intentify output blobs for yolo8-seg converter."));
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
        std::throw_with_nested(std::runtime_error("Failed to do YoloV8-SEG post-processing."));
    }
    return TensorsTable{};
}