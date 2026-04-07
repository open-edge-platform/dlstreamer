/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "meta_attacher.h"

#include "gmutex_lock_guard.h"
#include "gva_utils.h"
#include "processor_types.h"
#include <gst/analytics/analytics.h>

#include <exception>

using namespace post_processing;

MetaAttacher::Ptr MetaAttacher::create(ConverterType converter_type, AttachType attach_type) {
    switch (converter_type) {
    case ConverterType::TO_ROI:
        return MetaAttacher::Ptr(new ROIToFrameAttacher());
    case ConverterType::RAW:
    case ConverterType::TO_TENSOR: {
        switch (attach_type) {
        case AttachType::TO_FRAME:
            return MetaAttacher::Ptr(new TensorToFrameAttacher());
        case AttachType::TO_ROI:
            return MetaAttacher::Ptr(new TensorToROIAttacher());
        case AttachType::FOR_MICRO:
            return MetaAttacher::Ptr(new TensorToFrameAttacherForMicro());
        default:
            throw std::runtime_error("Unknown inference region");
        }
    }
    default:
        throw std::runtime_error("Unknown inference type");
    }
}

void ROIToFrameAttacher::attach(const TensorsTable &tensors, FramesWrapper &frames,
                                const BlobToMetaConverter &blob_to_meta) {
    checkFramesAndTensorsTable(frames, tensors);

    for (size_t i = 0; i < frames.size(); ++i) {
        auto &frame = frames[i];
        const auto &tensor = tensors[i];
        GstAnalyticsClsMtd cls_descriptor_mtd = {0, nullptr};

        for (size_t j = 0; j < tensor.size(); ++j) {
            GstStructure *detection_tensor = tensor[j][DETECTION_TENSOR_ID];

            uint32_t x_abs, y_abs, w_abs, h_abs;
            gst_structure_get_uint(detection_tensor, "x_abs", &x_abs);
            gst_structure_get_uint(detection_tensor, "y_abs", &y_abs);
            gst_structure_get_uint(detection_tensor, "w_abs", &w_abs);
            gst_structure_get_uint(detection_tensor, "h_abs", &h_abs);

            if (w_abs == 0 || h_abs == 0)
                continue;

            const gchar *label = gst_structure_get_string(detection_tensor, "label");

            GstBuffer **writable_buffer = &frame.buffer;
            gva_buffer_check_and_make_writable(writable_buffer, PRETTY_FUNCTION_NAME);

            GMutexLockGuard guard(frame.meta_mutex);
            GQuark gquark_label = g_quark_from_string(label);

            gdouble conf;
            gst_structure_get_double(detection_tensor, "confidence", &conf);

            GstAnalyticsRelationMeta *relation_meta = gst_buffer_add_analytics_relation_meta(*writable_buffer);

            if (not relation_meta)
                throw std::runtime_error("Failed to add GstAnalyticsRelationMeta to buffer");

            const auto &labels = blob_to_meta.getLabels();
            if (j == 0 && !labels.empty()) {
                gsize length = labels.size();
                std::vector<gfloat> confidence_levels(length, 0.0f);
                std::vector<GQuark> class_quarks(length, 0);

                for (size_t i = 0; i < length; i++) {
                    class_quarks[i] = g_quark_from_string(labels[i].c_str());
                }

                // find or create class descriptor metadata
                bool found = false;
                gpointer state = NULL;

                // check if class descriptor meta already exists
                while (gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_cls_mtd_get_mtd_type(),
                                                           &cls_descriptor_mtd)) {
                    if (gst_analytics_cls_mtd_get_length(&cls_descriptor_mtd) == length) {
                        bool skip = false;
                        for (size_t k = 0; k < length; k++) {
                            if (gst_analytics_cls_mtd_get_quark(&cls_descriptor_mtd, k) != class_quarks[k]) {
                                skip = true;
                                break;
                            }
                        }

                        if (skip) {
                            continue;
                        }

                        found = true;
                        break;
                    }
                }

                // create class descriptor if one does not exists
                if (!found) {
                    if (!gst_analytics_relation_meta_add_cls_mtd(relation_meta, length, confidence_levels.data(),
                                                                 class_quarks.data(), &cls_descriptor_mtd)) {
                        throw std::runtime_error("Failed to add class descriptor to meta");
                    }
                }
            }

            gdouble rotation = 0;
            gst_structure_get_double(detection_tensor, "rotation", &rotation);

            GstAnalyticsODMtd od_mtd;
            if (!gst_analytics_relation_meta_add_oriented_od_mtd(relation_meta, gquark_label, x_abs, y_abs, w_abs,
                                                                 h_abs, rotation, conf, &od_mtd)) {
                throw std::runtime_error("Failed to add detection data to meta");
            }

            if (label && cls_descriptor_mtd.meta == relation_meta) {
                if (!gst_analytics_relation_meta_set_relation(relation_meta, GST_ANALYTICS_REL_TYPE_RELATE_TO,
                                                              od_mtd.id, cls_descriptor_mtd.id)) {
                    throw std::runtime_error(
                        "Failed to set relation between object detection metadata and class descriptor metadata");
                }
            }

            for (size_t k = 0; k < tensor[j].size(); k++) {
                GstAnalyticsMtd tensor_mtd;
                GVA::Tensor gva_tensor(tensor[j][k]);
                if (gva_tensor.convert_to_meta(&tensor_mtd, &od_mtd, relation_meta)) {
                    if (!gst_analytics_relation_meta_set_relation(relation_meta, GST_ANALYTICS_REL_TYPE_CONTAIN,
                                                                  od_mtd.id, tensor_mtd.id)) {
                        throw std::runtime_error(
                            "Failed to set relation between object detection metadata and tensor metadata");
                    }
                    if (!gst_analytics_relation_meta_set_relation(relation_meta, GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                                                  tensor_mtd.id, od_mtd.id)) {
                        throw std::runtime_error(
                            "Failed to set relation between tensor metadata and object detection metadata");
                    }
                }
            }

            GstVideoRegionOfInterestMeta *roi_meta =
                gst_buffer_add_video_region_of_interest_meta(*writable_buffer, label, x_abs, y_abs, w_abs, h_abs);

            if (not roi_meta)
                throw std::runtime_error("Failed to add GstVideoRegionOfInterestMeta to buffer");

            roi_meta->id = od_mtd.id;
            if (frame.roi) {
                roi_meta->parent_id = frame.roi->id;

                if (frame.roi->id >= 0) {
                    GstAnalyticsODMtd parent_od_mtd;
                    if (gst_analytics_relation_meta_get_od_mtd(relation_meta, frame.roi->id, &parent_od_mtd)) {
                        if (!gst_analytics_relation_meta_set_relation(relation_meta, GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                                                      od_mtd.id, parent_od_mtd.id)) {
                            throw std::runtime_error(
                                "Failed to set relation between object detection metadata and parent metadata");
                        }

                        if (!gst_analytics_relation_meta_set_relation(relation_meta, GST_ANALYTICS_REL_TYPE_CONTAIN,
                                                                      parent_od_mtd.id, od_mtd.id)) {
                            throw std::runtime_error(
                                "Failed to set relation between object detection metadata and parent metadata");
                        }
                    }
                }
            }

            gst_structure_remove_field(detection_tensor, "label");
            gst_structure_remove_field(detection_tensor, "x_abs");
            gst_structure_remove_field(detection_tensor, "y_abs");
            gst_structure_remove_field(detection_tensor, "w_abs");
            gst_structure_remove_field(detection_tensor, "h_abs");

            for (size_t k = 0; k < tensor[j].size(); k++) {
                gst_video_region_of_interest_meta_add_param(roi_meta, tensor[j][k]);
            }
        }
    }
}

void TensorToFrameAttacher::attach(const TensorsTable &tensors_batch, FramesWrapper &frames,
                                   const BlobToMetaConverter &blob_to_meta) {
    (void)blob_to_meta;
    checkFramesAndTensorsTable(frames, tensors_batch);

    for (size_t i = 0; i < frames.size(); ++i) {
        GstBuffer **writable_buffer = &frames[i].buffer;

        for (std::vector<GstStructure *> tensor_data : tensors_batch[i]) {
            gva_buffer_check_and_make_writable(writable_buffer, PRETTY_FUNCTION_NAME);
            GstGVATensorMeta *tensor = GST_GVA_TENSOR_META_ADD(*writable_buffer);
            /* Tensor Meta already creates GstStructure during initialization */
            /* TODO: reduce amount of GstStructures copy from loading model-proc till attaching meta */
            if (tensor->data) {
                gst_structure_free(tensor->data);
            }
            assert(tensor_data.size() == 1);
            tensor->data = tensor_data[0];
            gst_structure_set(tensor->data, "element_id", G_TYPE_STRING, frames[i].model_instance_id.c_str(), NULL);
        }
    }
}

void TensorToROIAttacher::attach(const TensorsTable &tensors_batch, FramesWrapper &frames,
                                 const BlobToMetaConverter &blob_to_meta) {
    checkFramesAndTensorsTable(frames, tensors_batch);

    for (size_t i = 0; i < frames.size(); ++i) {
        GstBuffer *buffer = frames[i].buffer;

        GMutexLockGuard guard(frames[i].meta_mutex);
        GstAnalyticsODMtd od_meta;
        if (!findODMeta(buffer, frames[i].roi, &od_meta)) {
            GST_WARNING("No detection tensors were found for this buffer in case of roi-list inference.");
            continue;
        }

        GstAnalyticsClsMtd cls_descriptor_mtd = {0, nullptr};
        const auto &labels = blob_to_meta.getLabels();
        if (!labels.empty()) {
            gsize length = labels.size();
            std::vector<gfloat> confidence_levels(length, 0.0f);
            std::vector<GQuark> class_quarks(length, 0);

            for (size_t i = 0; i < length; i++) {
                class_quarks[i] = g_quark_from_string(labels[i].c_str());
            }

            // find or create class descriptor metadata
            bool found = false;
            gpointer state = NULL;

            // check if class descriptor meta already exists
            while (gst_analytics_relation_meta_iterate(od_meta.meta, &state, gst_analytics_cls_mtd_get_mtd_type(),
                                                       &cls_descriptor_mtd)) {
                if (gst_analytics_cls_mtd_get_length(&cls_descriptor_mtd) == length) {
                    bool skip = false;
                    for (size_t k = 0; k < length; k++) {
                        if (gst_analytics_cls_mtd_get_quark(&cls_descriptor_mtd, k) != class_quarks[k]) {
                            skip = true;
                            break;
                        }
                    }

                    if (skip) {
                        continue;
                    }

                    found = true;
                    break;
                }
            }

            // create class descriptor if one does not exists
            if (!found) {
                if (!gst_analytics_relation_meta_add_cls_mtd(od_meta.meta, length, confidence_levels.data(),
                                                             class_quarks.data(), &cls_descriptor_mtd)) {
                    throw std::runtime_error("Failed to add class descriptor to meta");
                }
            }
        }

        for (std::vector<GstStructure *> tensor_data : tensors_batch[i]) {
            assert(tensor_data.size() == 1);
            GstAnalyticsMtd tensor_mtd;
            GVA::Tensor gva_tensor(tensor_data[0]);
            if (gva_tensor.convert_to_meta(&tensor_mtd, &od_meta, od_meta.meta)) {
                if (!gst_analytics_relation_meta_set_relation(od_meta.meta, GST_ANALYTICS_REL_TYPE_CONTAIN, od_meta.id,
                                                              tensor_mtd.id)) {
                    throw std::runtime_error(
                        "Failed to set relation between object detection metadata and tensor metadata");
                }

                if (!gst_analytics_relation_meta_set_relation(od_meta.meta, GST_ANALYTICS_REL_TYPE_IS_PART_OF,
                                                              tensor_mtd.id, od_meta.id)) {
                    throw std::runtime_error(
                        "Failed to set relation between tensor metadata and object detection metadata");
                }

                if (gva_tensor.has_field("label_id") && od_meta.meta == cls_descriptor_mtd.meta) {
                    if (!gst_analytics_relation_meta_set_relation(od_meta.meta, GST_ANALYTICS_REL_TYPE_RELATE_TO,
                                                                  tensor_mtd.id, cls_descriptor_mtd.id)) {
                        throw std::runtime_error(
                            "Failed to set relation between tensor metadata and class descriptor metadata");
                    }
                }
            }
        }

        GstVideoRegionOfInterestMeta *roi_meta = findROIMeta(buffer, frames[i].roi);
        if (!roi_meta) {
            GST_WARNING("No detection tensors were found for this buffer in case of roi-list inference.");
            continue;
        }

        for (std::vector<GstStructure *> tensor_data : tensors_batch[i]) {
            assert(tensor_data.size() == 1);
            gst_video_region_of_interest_meta_add_param(roi_meta, tensor_data[0]);
            frames[i].roi_classifications->push_back(tensor_data[0]);
        }
    }
}

void TensorToFrameAttacherForMicro::attach(const TensorsTable &tensors, FramesWrapper &frames,
                                           const BlobToMetaConverter &blob_to_meta) {
    (void)blob_to_meta;

    if (tensors.size() == 0) {
        return;
    }

    // TODO: adjust for batch size
    if (frames.size() != 1) {
        throw std::runtime_error("Failed to attach tensor to frame: Batch size is not supported in micro currently.");
    }
    for (size_t i = 0; i < frames.size(); ++i) {
        auto &frame = frames[i];

        for (std::vector<GstStructure *> tensor_data : tensors[i]) {
            auto tensor = GST_GVA_TENSOR_META_ADD(frame.buffer);
            if (tensor->data) {
                gst_structure_free(tensor->data);
            }
            assert(tensor_data.size() == 1);
            tensor->data = tensor_data[0];
            gst_structure_set(tensor->data, "element_id", G_TYPE_STRING, frame.model_instance_id.c_str(), NULL);
        }
    }
}

GstVideoRegionOfInterestMeta *TensorToROIAttacher::findROIMeta(GstBuffer *buffer,
                                                               GstVideoRegionOfInterestMeta *frame_roi) {
    GstVideoRegionOfInterestMeta *meta = nullptr;
    gpointer state = nullptr;
    while ((meta = GST_VIDEO_REGION_OF_INTEREST_META_ITERATE(buffer, &state))) {
        if (sameRegion(meta, frame_roi)) {
            return meta;
        }
    }
    return meta;
}

bool TensorToROIAttacher::findODMeta(GstBuffer *buffer, GstVideoRegionOfInterestMeta *frame_roi,
                                     GstAnalyticsODMtd *rlt_mtd) {
    gpointer state = nullptr;
    GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);

    if (relation_meta) {
        while (
            gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_od_mtd_get_mtd_type(), rlt_mtd)) {
            if (sameRegion(rlt_mtd, frame_roi)) {
                return true;
            }
        }
    }
    return false;
}
