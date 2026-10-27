/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "gstgvaclassify.h"

#include <gst/analytics/analytics.h>
#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

struct ClassificationHistory;
struct ClassificationHistory *create_classification_history(GstGvaClassify *gva_classify);
void release_classification_history(struct ClassificationHistory *classification_history);
void fill_roi_params_from_history(struct ClassificationHistory *classification_history, GstBuffer *buffer);
void clear_classification_history(struct ClassificationHistory *classification_history);

G_END_DECLS

#ifdef __cplusplus
#include "gst_smart_pointer_types.hpp"
#include "lru_cache.h"

#include <mutex>
#include <vector>

const size_t CLASSIFICATION_HISTORY_SIZE = 100;

struct ClassificationHistory {
  public:
    struct ClassificationResult {
        GstStructureSharedPtr tensor;
        // Labels of the class descriptor the result was related to (empty if none), required to restore label_id
        std::vector<GQuark> class_descriptor;
    };

    struct ROIClassificationHistory {
        // Input frame number on which (re)classification was scheduled last time
        uint64_t frame_of_last_update;
        // Output frame number which carries the most recent results
        uint64_t frame_of_last_result;
        std::vector<ClassificationResult> results;

        ROIClassificationHistory(uint64_t frame_of_last_update = {}, uint64_t frame_of_last_result = {},
                                 std::vector<ClassificationResult> results = {})
            : frame_of_last_update(frame_of_last_update), frame_of_last_result(frame_of_last_result),
              results(std::move(results)) {
        }
    };

    ClassificationHistory(GstGvaClassify *gva_classify);

    bool IsROIClassificationNeeded(GstVideoRegionOfInterestMeta *roi, GstBuffer *buffer, uint64_t current_num_frame);
    void UpdateROIParams(GstBuffer *buffer, GstVideoRegionOfInterestMeta roi,
                         const std::vector<GstStructure *> &classifications);
    void FillROIParams(GstBuffer *buffer);
    void Clear();
    LRUCache<int, ROIClassificationHistory> &GetHistory();

  private:
    void CheckExistingAndReaddObjectId(int object_id);

    GstGvaClassify *gva_classify;
    // Number of frames which left the element, counted on src pad in the order frames are pushed
    uint64_t output_frame_num;
    LRUCache<int, ROIClassificationHistory> history;
    std::mutex history_mutex;
};
#endif
