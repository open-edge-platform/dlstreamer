/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
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

G_END_DECLS

#ifdef __cplusplus
#include "gst_smart_pointer_types.hpp"
#include "lru_cache.h"

#include <map>
#include <mutex>

const size_t CLASSIFICATION_HISTORY_SIZE = 100;

struct ClassificationHistory {
  public:
    struct ROIClassificationHistory {
        uint64_t frame_of_last_update;
        std::map<std::string, GstStructureSharedPtr> layers_to_roi_params;

        ROIClassificationHistory(uint64_t frame_of_last_update = {},
                                 std::map<std::string, GstStructureSharedPtr> layers_to_roi_params = {})
            : frame_of_last_update(frame_of_last_update), layers_to_roi_params(layers_to_roi_params) {
        }
    };

    ClassificationHistory(GstGvaClassify *gva_classify);

    bool IsROIClassificationNeeded(GstVideoRegionOfInterestMeta *roi, GstBuffer *buffer, uint64_t current_num_frame);
    void UpdateROIParams(int roi_id, const GstStructure *roi_param);
    void FillROIParams(GstBuffer *buffer);
    LRUCache<int, ROIClassificationHistory> &GetHistory();

  private:
    void CheckExistingAndReaddObjectId(int roi_id);

    GstGvaClassify *gva_classify;
    uint64_t current_num_frame;
    LRUCache<int, ROIClassificationHistory> history;
    std::mutex history_mutex;
};
#endif
