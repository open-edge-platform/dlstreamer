/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "classification_history.h"
#include "gmutex_lock_guard.h"
#include "gva_utils.h"
#include "inference_backend/logger.h"
#include "inference_impl.h"
#include "utils.h"
#include <video_frame.h>

#include <algorithm>

ClassificationHistory::ClassificationHistory(GstGvaClassify *gva_classify)
    : gva_classify(gva_classify), current_num_frame(0), history(CLASSIFICATION_HISTORY_SIZE) {
}

bool ClassificationHistory::IsROIClassificationNeeded(GstVideoRegionOfInterestMeta *roi, GstBuffer *buffer,
                                                      uint64_t current_num_frame) {
    try {
        std::lock_guard<std::mutex> guard(history_mutex);
        this->current_num_frame = current_num_frame;

        // by default we assume that
        // we have recent classification result or classification is not required for this object
        bool result = false;
        gint id;
        if (roi->id >= 0) {
            GMutexLockGuard guard(&gva_classify->base_inference.meta_mutex);
            GstAnalyticsRelationMeta *relation_meta = gst_buffer_get_analytics_relation_meta(buffer);
            if (!relation_meta) {
                throw std::runtime_error("Failed to get GstAnalyticsRelationMeta from buffer");
            }

            GstAnalyticsODMtd od_mtd;
            if (!gst_analytics_relation_meta_get_od_mtd(relation_meta, roi->id, &od_mtd)) {
                throw std::runtime_error("Failed to get object detection metadata");
            }

            if (!get_od_id(od_mtd, &id))
                // object has not been tracked
                return true;
        }

        if (history.count(id) == 0) { // new object
            history.put(id);
            history.get(id).frame_of_last_update = current_num_frame;
            result = true;
        } else if (gva_classify->reclassify_interval == 0) {
            return false;
        } else {
            auto current_interval = current_num_frame - history.get(id).frame_of_last_update;
            if (current_interval > INT64_MAX && history.get(id).frame_of_last_update > current_num_frame)
                current_interval = (UINT64_MAX - history.get(id).frame_of_last_update) + current_num_frame + 1;
            if (current_interval >= gva_classify->reclassify_interval) {
                // new object or reclassify old object
                history.get(id).frame_of_last_update = current_num_frame;
                result = true;
            }
        }

        return result;
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to check if detection tensor classification needed"));
    }
}

void ClassificationHistory::UpdateROIParams(int roi_id, const GstStructure *roi_param) {
    try {
        std::lock_guard<std::mutex> guard(history_mutex);

        const gchar *layer_c = gst_structure_get_name(roi_param);
        if (not layer_c)
            throw std::runtime_error("Can't get name of region of interest param structure");
        std::string layer(layer_c);

        // To prevent attempts to access removed objects,
        // we should readd lost objects to history if needed
        CheckExistingAndReaddObjectId(roi_id);

        history.get(roi_id).layers_to_roi_params[layer] =
            GstStructureSharedPtr(gst_structure_copy(roi_param), gst_structure_free);
    } catch (const std::exception &e) {
        std::throw_with_nested(std::runtime_error("Failed to update detection tensor parameters"));
    }
}

void ClassificationHistory::FillROIParams(GstBuffer *buffer) {
    try {
        GVA::VideoFrame video_frame(buffer, gva_classify->base_inference.info);
        std::lock_guard<std::mutex> guard(history_mutex);
        for (GVA::RegionOfInterest &region : video_frame.regions()) {
            gint id = region.object_id();
            if (!id)
                continue;
            InferenceImpl *inference = gva_classify->base_inference.inference;
            assert(inference && "Empty inference instance");
            bool is_appropriate_object_class = inference->FilterObjectClass(region.label());
            if (history.count(id) && is_appropriate_object_class) {
                const auto &roi_history = history.get(id);
                int frames_ago = this->current_num_frame - roi_history.frame_of_last_update;
                for (const auto &layer_to_roi_param : roi_history.layers_to_roi_params) {
                    if (!region.get_param(gst_structure_get_name(layer_to_roi_param.second.get()))) {
                        auto tensor = GstStructureUniquePtr(gst_structure_copy(layer_to_roi_param.second.get()),
                                                            gst_structure_free);
                        if (not tensor)
                            throw std::runtime_error("Failed to create classification tensor");
                        gst_structure_set(tensor.get(), "frames_ago", G_TYPE_INT, frames_ago, NULL);
                        region.add_param(tensor.release());
                    }
                }
            }
        }
    } catch (const std::exception &e) {
        GVA_ERROR("Failed to fill detection tensor parameters from history:\n%s",
                  Utils::createNestedErrorMsg(e).c_str());
    }
}

LRUCache<int, ClassificationHistory::ROIClassificationHistory> &ClassificationHistory::GetHistory() {
    std::lock_guard<std::mutex> guard(history_mutex);
    return history;
}

void ClassificationHistory::CheckExistingAndReaddObjectId(int roi_id) {
    if (history.count(roi_id) == 0) {
        GVA_WARNING("Classification history size limit is exceeded. "
                    "Additional reclassification within reclassify-interval is required.");
        GvaBaseInference *base_inference = GVA_BASE_INFERENCE(gva_classify);
        current_num_frame = base_inference->frame_num;
        history.put(roi_id);
        history.get(roi_id).frame_of_last_update = current_num_frame;
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
