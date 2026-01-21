/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "blob_to_roi_converter.h"
#include "boxes.h"
#include "boxes_labels.h"
#include "boxes_scores.h"
#include "centerface.h"
#include "custom_to_roi.h"
#include "detection_output.h"
#include "mask_rcnn.h"
#include "yolo_base.h"
#include "yolo_v10.h"
#include "yolo_v11.h"
#include "yolo_v2.h"
#include "yolo_v26.h"
#include "yolo_v3.h"
#include "yolo_v4.h"
#include "yolo_v5.h"
#include "yolo_v7.h"
#include "yolo_v8.h"
#include "yolo_x.h"

#include "inference_backend/logger.h"

#include <gst/gst.h>

#include <algorithm>
#include <cassert>
#include <exception>
#include <map>
#include <memory>
#include <string>

using namespace post_processing;

BlobToMetaConverter::Ptr BlobToROIConverter::create(BlobToMetaConverter::Initializer initializer,
                                                    const std::string &converter_name,
                                                    const std::string &custom_postproc_lib) {
    auto &model_proc_output_info = initializer.model_proc_output_info;
    if (model_proc_output_info == nullptr)
        throw std::runtime_error("model_proc_output_info have been hot initialized.");

    double confidence_threshold;
    if (not gst_structure_get_double(model_proc_output_info.get(), "confidence_threshold", &confidence_threshold))
        throw std::runtime_error("Post-processor parameter undefined: confidence_threshold.");
    double iou_threshold = DEFAULT_IOU_THRESHOLD;
    gst_structure_get_double(model_proc_output_info.get(), "iou_threshold", &iou_threshold);

    if (!custom_postproc_lib.empty())
        return BlobToMetaConverter::Ptr(
            new CustomToRoiConverter(std::move(initializer), confidence_threshold, iou_threshold, custom_postproc_lib));
    else if (converter_name == DetectionOutputConverter::getName())
        return BlobToMetaConverter::Ptr(new DetectionOutputConverter(std::move(initializer), confidence_threshold));
    else if (converter_name == BoxesLabelsConverter::getName())
        return BlobToMetaConverter::Ptr(new BoxesLabelsConverter(std::move(initializer), confidence_threshold));
    else if (converter_name == BoxesScoresConverter::getName())
        return BlobToMetaConverter::Ptr(new BoxesScoresConverter(std::move(initializer), confidence_threshold));
    else if (converter_name == BoxesConverter::getName())
        return BlobToMetaConverter::Ptr(new BoxesConverter(std::move(initializer), confidence_threshold));
    else if (converter_name == YOLOv2Converter::getName() || converter_name == YOLOv3Converter::getName() ||
             converter_name == YOLOv4Converter::getName() || converter_name == YOLOv5Converter::getName())
        return YOLOBaseConverter::create(std::move(initializer), converter_name, confidence_threshold);
    else if (converter_name == YOLOv7Converter::getName())
        return BlobToMetaConverter::Ptr(
            new YOLOv7Converter(std::move(initializer), confidence_threshold, iou_threshold));
    else if (converter_name == YOLOv8Converter::getName() || converter_name == YOLOv11Converter::getName())
        return BlobToMetaConverter::Ptr(
            new YOLOv8Converter(std::move(initializer), confidence_threshold, iou_threshold));
    else if (converter_name == YOLOv8ObbConverter::getName() || converter_name == YOLOv11ObbConverter::getName())
        return BlobToMetaConverter::Ptr(
            new YOLOv8ObbConverter(std::move(initializer), confidence_threshold, iou_threshold));
    else if (converter_name == YOLOv8PoseConverter::getName() || converter_name == YOLOv11PoseConverter::getName())
        return BlobToMetaConverter::Ptr(
            new YOLOv8PoseConverter(std::move(initializer), confidence_threshold, iou_threshold));
    else if (converter_name == YOLOv8SegConverter::getName() || converter_name == YOLOv11SegConverter::getName())
        return BlobToMetaConverter::Ptr(
            new YOLOv8SegConverter(std::move(initializer), confidence_threshold, iou_threshold));
    else if (converter_name == YOLOv10Converter::getName() || converter_name == YOLOv26Converter::getName())
        return BlobToMetaConverter::Ptr(
            new YOLOv10Converter(std::move(initializer), confidence_threshold, iou_threshold));
    else if (converter_name == YOLOv26ObbConverter::getName())
        return BlobToMetaConverter::Ptr(
            new YOLOv26ObbConverter(std::move(initializer), confidence_threshold, iou_threshold));
    else if (converter_name == YOLOv26PoseConverter::getName())
        return BlobToMetaConverter::Ptr(
            new YOLOv26PoseConverter(std::move(initializer), confidence_threshold, iou_threshold));
    else if (converter_name == YOLOv26SegConverter::getName())
        return BlobToMetaConverter::Ptr(
            new YOLOv26SegConverter(std::move(initializer), confidence_threshold, iou_threshold));
    else if (converter_name == MaskRCNNConverter::getName())
        return BlobToMetaConverter::Ptr(
            new MaskRCNNConverter(std::move(initializer), confidence_threshold, iou_threshold));
    else if (converter_name == CenterfaceConverter::getName())
        return BlobToMetaConverter::Ptr(
            new CenterfaceConverter(std::move(initializer), confidence_threshold, iou_threshold));
    else if (converter_name == YOLOxConverter::getName()) {
        int classes;
        if (not gst_structure_get_int(model_proc_output_info.get(), "classes", &classes))
            throw std::runtime_error("Post-processor parameter undefined: classes.");
        return BlobToMetaConverter::Ptr(
            new YOLOxConverter(std::move(initializer), confidence_threshold, iou_threshold, classes));
    }

    throw std::runtime_error("ToROIConverter \"" + converter_name + "\" is not implemented.");
}

TensorsTable BlobToROIConverter::toTensorsTable(const DetectedObjectsTable &bboxes_table) const {
    size_t batch_size = getModelInputImageInfo().batch_size;

    if (bboxes_table.size() != batch_size)
        throw std::logic_error("bboxes_table and batch_size must be equal.");

    TensorsTable tensors_table(batch_size);

    for (size_t image_id = 0; image_id < batch_size; ++image_id) {
        auto &tensors_batch = tensors_table[image_id];
        const auto &bboxes = bboxes_table[image_id];
        for (const DetectedObject &object : bboxes) {
            tensors_batch.emplace_back(object.toTensor(getModelProcOutputInfo()));
        }
    }

    return tensors_table;
}

TensorsTable BlobToROIConverter::storeObjects(DetectedObjectsTable &objects_table) const {
    ITT_TASK(__FUNCTION__);
    if (need_nms)
        for (auto &objects : objects_table)
            runNms(objects);

    return toTensorsTable(objects_table);
}

void BlobToROIConverter::runNms(std::vector<DetectedObject> &candidates) const {
    ITT_TASK(__FUNCTION__);
    std::sort(candidates.rbegin(), candidates.rend());

    for (auto p_first_candidate = candidates.begin(); p_first_candidate != candidates.end(); ++p_first_candidate) {
        const auto &first_candidate = *p_first_candidate;
        double first_candidate_area = first_candidate.w * first_candidate.h;

        for (auto p_candidate = p_first_candidate + 1; p_candidate != candidates.end();) {
            const auto &candidate = *p_candidate;

            const double inter_width = std::min(first_candidate.x + first_candidate.w, candidate.x + candidate.w) -
                                       std::max(first_candidate.x, candidate.x);
            const double inter_height = std::min(first_candidate.y + first_candidate.h, candidate.y + candidate.h) -
                                        std::max(first_candidate.y, candidate.y);
            if (inter_width <= 0.0 || inter_height <= 0.0) {
                ++p_candidate;
                continue;
            }

            const double inter_area = inter_width * inter_height;
            const double candidate_area = candidate.w * candidate.h;
            const double union_area = candidate_area + first_candidate_area - inter_area;

            assert(union_area != 0 && "union_area is null. Both of the boxes have zero areas.");
            const double overlap = inter_area / union_area;
            if (overlap > iou_threshold) {
                for (auto tensor : p_candidate->tensors) {
                    gst_structure_free(tensor);
                }
                p_candidate = candidates.erase(p_candidate);
            } else
                ++p_candidate;
        }
    }
}
