/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include <map>
#include <string>
#include <vector>

#include "classification_history.h"
#include "gstgvaclassify.h"
#include "gva_base_inference.h"
#include "inference_impl.h"
#include "region_of_interest.h"
#include "safe_arithmetic.hpp"
#include "utils.h"

#include <inference_backend/image_inference.h>
#include <opencv2/imgproc.hpp>

#include "pre_processors.h"

namespace {

using namespace InferenceBackend;

bool IsROIClassificationNeeded(GvaBaseInference *gva_base_inference, guint64 current_num_frame, GstBuffer *buffer,
                               GstVideoRegionOfInterestMeta *roi) {
    GstGvaClassify *gva_classify = GST_GVA_CLASSIFY(gva_base_inference);
    assert(gva_classify->classification_history != NULL);

    // Check is object recently classified
    return (gva_classify->reclassify_interval == 1 ||
            gva_classify->classification_history->IsROIClassificationNeeded(roi, buffer, current_num_frame));
}

} // anonymous namespace

FilterROIFunction IS_ROI_CLASSIFICATION_NEEDED = IsROIClassificationNeeded;
