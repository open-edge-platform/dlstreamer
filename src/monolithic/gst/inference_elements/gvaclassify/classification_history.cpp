/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "classification_history.h"
#include "gmutex_lock_guard.h"
#include "gva_utils.h"
#include "inference_backend/logger.h"
#include "inference_impl.h"
#include "post_processor/post_proc_common.h"
#include "utils.h"
#include <dlstreamer/gst/videoanalytics/tensor.h>

#include <cstring>

namespace {

constexpr const char *CLASS_DESCRIPTOR_TAG = "class_descriptor";

bool IsClassDescriptor(GstAnalyticsClsMtd *cls_mtd) {
    gchar *tag = gst_analytics_mtd_get_semantic_tag(reinterpret_cast<GstAnalyticsMtd *>(cls_mtd));
    bool result = tag && strcmp(tag, CLASS_DESCRIPTOR_TAG) == 0;
    g_free(tag);
    return result;
}

std::vector<GQuark> GetClassDescriptorLabels(GstAnalyticsClsMtd *cls_descriptor) {
    gsize length = gst_analytics_cls_mtd_get_length(cls_descriptor);
    std::vector<GQuark> labels(length);
    for (gsize i = 0; i < length; ++i)
        labels[i] = gst_analytics_cls_mtd_get_quark(cls_descriptor, i);
    return labels;
}

// Find class descriptor which was related (by meta attacher) to classification result 'tensor' of object 'od_mtd'
std::vector<GQuark> FindClassDescriptorLabels(GstAnalyticsODMtd *od_mtd, const GVA::Tensor &tensor) {
    if (tensor.type() != GVA::GST_ANALYTICS_CLS_2_TENSOR || !tensor.has_field("label_id"))
        return {};

    const GQuark label = g_quark_from_string(tensor.label().c_str());
    const gint label_id = tensor.get_int("label_id", -1);
    if (label_id < 0)
        return {};

    gpointer state = nullptr;
    GstAnalyticsClsMtd cls_mtd;
    while (gst_analytics_relation_meta_get_direct_related(od_mtd->meta, od_mtd->id, GST_ANALYTICS_REL_TYPE_CONTAIN,
                                                          gst_analytics_cls_mtd_get_mtd_type(), &state, &cls_mtd)) {
        if (gst_analytics_cls_mtd_get_length(&cls_mtd) != 1 || gst_analytics_cls_mtd_get_quark(&cls_mtd, 0) != label)
            continue;

        GstAnalyticsClsMtd cls_descriptor;
        if (gst_analytics_relation_meta_get_direct_related(cls_mtd.meta, cls_mtd.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
                                                           gst_analytics_cls_mtd_get_mtd_type(), nullptr,
                                                           &cls_descriptor) &&
            IsClassDescriptor(&cls_descriptor) && gst_analytics_cls_mtd_get_index_by_quark(&cls_descriptor, label) ==
                                                      label_id) {
            return GetClassDescriptorLabels(&cls_descriptor);
        }
    }
    return {};
}

GstAnalyticsClsMtd FindOrAddClassDescriptor(GstAnalyticsRelationMeta *relation_meta,
                                            const std::vector<GQuark> &labels) {
    GstAnalyticsClsMtd cls_descriptor;
    gpointer state = nullptr;
    while (gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_cls_mtd_get_mtd_type(),
                                               &cls_descriptor)) {
        if (IsClassDescriptor(&cls_descriptor) && GetClassDescriptorLabels(&cls_descriptor) == labels)
            return cls_descriptor;
    }

    std::vector<gfloat> confidence_levels(labels.size(), 0.0f);
    std::vector<GQuark> class_quarks(labels);
    if (!gst_analytics_relation_meta_add_cls_mtd(relation_meta, class_quarks.size(), confidence_levels.data(),
                                                 class_quarks.data(), &cls_descriptor))
        throw std::runtime_error("Failed to add class descriptor metadata");
    gst_analytics_mtd_set_semantic_tag(reinterpret_cast<GstAnalyticsMtd *>(&cls_descriptor), CLASS_DESCRIPTOR_TAG);
    return cls_descriptor;
}

// TODO: remove when all consumers (e.g. gvametaconvert) read classification results from GstAnalytics metadata
void AttachLegacyResult(GstVideoRegionOfInterestMeta *roi_meta, const ClassificationHistory::ClassificationResult &result,
                        gint frames_ago) {
    if (!roi_meta || gst_video_region_of_interest_meta_get_param(roi_meta, gst_structure_get_name(result.tensor.get())))
        return;

    GstStructure *tensor = gst_structure_copy(result.tensor.get());
    if (!tensor)
        throw std::runtime_error("Failed to create classification tensor");
    gst_structure_set(tensor, "frames_ago", G_TYPE_INT, frames_ago, NULL);
    gst_video_region_of_interest_meta_add_param(roi_meta, tensor);
}

void AttachResult(GstAnalyticsODMtd *od_mtd, const ClassificationHistory::ClassificationResult &result) {
    GVA::Tensor tensor(result.tensor.get());

    gint x, y, w, h;
    gfloat confidence;
    if (!gst_analytics_od_mtd_get_location(od_mtd, &x, &y, &w, &h, &confidence))
        throw std::runtime_error("Failed to get object detection location");

    GstAnalyticsMtd result_mtd;
    if (!tensor.convert_to_meta(&result_mtd, od_mtd->meta, x, y, w, h)) {
        GST_DEBUG("Classification result of type '%s' has no GstAnalytics representation, skipping",
                  tensor.type().c_str());
        return;
    }

    if (!gst_analytics_relation_meta_set_relation(od_mtd->meta, GST_ANALYTICS_REL_TYPE_CONTAIN, od_mtd->id,
                                                  result_mtd.id) ||
        !gst_analytics_relation_meta_set_relation(od_mtd->meta, GST_ANALYTICS_REL_TYPE_IS_PART_OF, result_mtd.id,
                                                  od_mtd->id))
        throw std::runtime_error("Failed to set relation between object detection and classification metadata");

    if (!result.class_descriptor.empty()) {
        GstAnalyticsClsMtd cls_descriptor = FindOrAddClassDescriptor(od_mtd->meta, result.class_descriptor);
        if (!gst_analytics_relation_meta_set_relation(od_mtd->meta, GST_ANALYTICS_REL_TYPE_RELATE_TO, result_mtd.id,
                                                      cls_descriptor.id))
            throw std::runtime_error("Failed to set relation between classification and class descriptor metadata");
    }
}

} // namespace

ClassificationHistory::ClassificationHistory(GstGvaClassify *gva_classify)
    : gva_classify(gva_classify), output_frame_num(0), history(CLASSIFICATION_HISTORY_SIZE) {
}

bool ClassificationHistory::IsROIClassificationNeeded(GstVideoRegionOfInterestMeta *roi, GstBuffer *buffer,
                                                      uint64_t current_num_frame) {
    try {
        if (roi->id < 0)
            return true;

        std::lock_guard<std::mutex> guard(history_mutex);

        gint id = 0;
        {
            GMutexLockGuard meta_guard(&gva_classify->base_inference.meta_mutex);
            GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);
            if (!relation_meta)
                throw std::runtime_error("Failed to get GstAnalyticsRelationMeta from buffer");

            GstAnalyticsODMtd od_mtd;
            if (!gst_analytics_relation_meta_get_od_mtd(relation_meta, roi->id, &od_mtd))
                throw std::runtime_error("Failed to get object detection metadata");

            if (!get_od_id(od_mtd, &id))
                // object has not been tracked
                return true;
        }

        if (history.count(id) == 0) { // new object
            history.put(id);
            history.get(id).frame_of_last_update = current_num_frame;
            return true;
        }

        if (gva_classify->reclassify_interval == 0)
            return false;

        auto &roi_history = history.get(id);
        auto current_interval = current_num_frame - roi_history.frame_of_last_update;
        if (current_interval > INT64_MAX && roi_history.frame_of_last_update > current_num_frame)
            current_interval = (UINT64_MAX - roi_history.frame_of_last_update) + current_num_frame + 1;
        if (current_interval >= gva_classify->reclassify_interval) {
            roi_history.frame_of_last_update = current_num_frame;
            return true;
        }

        return false;
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to check if detection tensor classification needed"));
    }
}

void ClassificationHistory::UpdateROIParams(GstBuffer *buffer, GstVideoRegionOfInterestMeta roi,
                                            const std::vector<GstStructure *> &classifications) {
    if (classifications.empty())
        return;
    try {
        std::lock_guard<std::mutex> guard(history_mutex);

        gint object_id = 0;
        std::vector<ClassificationResult> results;
        {
            GMutexLockGuard meta_guard(&gva_classify->base_inference.meta_mutex);
            GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);
            if (!relation_meta)
                throw std::runtime_error("Failed to get GstAnalyticsRelationMeta from buffer");

            GstAnalyticsODMtd od_mtd;
            if (!gst_analytics_relation_meta_get_od_mtd(relation_meta, roi.id, &od_mtd))
                throw std::runtime_error("Failed to get object detection metadata");

            if (!post_processing::sameRegion(&od_mtd, &roi))
                throw std::runtime_error("Roi and object detection metadata are not the same region");

            if (!get_od_id(od_mtd, &object_id) || object_id == 0)
                return; // object has not been tracked

            for (const GstStructure *classification : classifications) {
                if (!classification)
                    throw std::runtime_error("Null classification result structure");
                GstStructureSharedPtr tensor(gst_structure_copy(classification), gst_structure_free);
                std::vector<GQuark> class_descriptor = FindClassDescriptorLabels(&od_mtd, GVA::Tensor(tensor.get()));
                results.push_back({std::move(tensor), std::move(class_descriptor)});
            }
        }

        // To prevent attempts to access removed objects, we should readd lost objects to history if needed
        CheckExistingAndReaddObjectId(object_id);

        auto &roi_history = history.get(object_id);
        roi_history.results = std::move(results);
        // This method is called right before the frame is pushed to src pad, so the frame will be counted
        // as 'output_frame_num' by FillROIParams
        roi_history.frame_of_last_result = output_frame_num;
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to update classification history"));
    }
}

void ClassificationHistory::FillROIParams(GstBuffer *buffer) {
    std::lock_guard<std::mutex> guard(history_mutex);
    const uint64_t frame_num = output_frame_num++;
    // Every frame is classified, history is not used
    if (gva_classify->reclassify_interval == 1)
        return;
    try {
        GMutexLockGuard meta_guard(&gva_classify->base_inference.meta_mutex);
        GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);
        if (!relation_meta)
            return;

        InferenceImpl *inference = gva_classify->base_inference.inference;
        assert(inference && "Empty inference instance");

        // Collect objects first, as attaching new metadata while iterating relation meta is not safe
        std::vector<std::pair<GstAnalyticsODMtd, gint>> tracked_objects;
        gpointer state = nullptr;
        GstAnalyticsODMtd od_mtd;
        while (gst_analytics_relation_meta_iterate(relation_meta, &state, gst_analytics_od_mtd_get_mtd_type(),
                                                   &od_mtd)) {
            gint object_id = 0;
            if (get_od_id(od_mtd, &object_id) && object_id != 0 && history.count(object_id))
                tracked_objects.emplace_back(od_mtd, object_id);
        }

        for (auto &[object_od_mtd, object_id] : tracked_objects) {
            const auto &roi_history = history.get(object_id);
            // Frame already carries results of classification
            if (roi_history.frame_of_last_result == frame_num)
                continue;

            GQuark label = gst_analytics_od_mtd_get_obj_type(&object_od_mtd);
            if (!inference->FilterObjectClass(label ? g_quark_to_string(label) : ""))
                continue;

            GstVideoRegionOfInterestMeta *roi_meta =
                gst_buffer_get_video_region_of_interest_meta_id(buffer, object_od_mtd.id);
            const gint frames_ago = static_cast<gint>(frame_num - roi_history.frame_of_last_result);
            for (const auto &result : roi_history.results) {
                AttachLegacyResult(roi_meta, result, frames_ago);
                AttachResult(&object_od_mtd, result);
            }
        }
    } catch (const std::exception &e) {
        GVA_ERROR("Failed to fill classification results from history:\n%s", Utils::createNestedErrorMsg(e).c_str());
    }
}

void ClassificationHistory::Clear() {
    std::lock_guard<std::mutex> guard(history_mutex);
    history.clear();
}

LRUCache<int, ClassificationHistory::ROIClassificationHistory> &ClassificationHistory::GetHistory() {
    std::lock_guard<std::mutex> guard(history_mutex);
    return history;
}

void ClassificationHistory::CheckExistingAndReaddObjectId(int object_id) {
    if (history.count(object_id) == 0) {
        GVA_WARNING("Classification history size limit is exceeded. "
                    "Additional reclassification within reclassify-interval is required.");
        GvaBaseInference *base_inference = GVA_BASE_INFERENCE(gva_classify);
        history.put(object_id);
        history.get(object_id).frame_of_last_update = base_inference->frame_num;
    }
}

ClassificationHistory *create_classification_history(GstGvaClassify *gva_classify) {
    try {
        return new ClassificationHistory(gva_classify);
    } catch (const std::exception &e) {
        GST_ELEMENT_ERROR(gva_classify, LIBRARY, INIT, ("gvaclassify intitialization failed"),
                          ("%s", Utils::createNestedErrorMsg(e).c_str()));
        return nullptr;
    }
}

void release_classification_history(ClassificationHistory *classification_history) {
    delete classification_history;
}

void fill_roi_params_from_history(ClassificationHistory *classification_history, GstBuffer *buffer) {
    classification_history->FillROIParams(buffer);
}

void clear_classification_history(ClassificationHistory *classification_history) {
    classification_history->Clear();
}
