/*******************************************************************************
 * Copyright (C) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_roi_converter.h"
#include "inference_backend/image_inference.h"
#include <opencv2/opencv.hpp>

#include <gst/gst.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace post_processing {

class CustomToRoiConverter : public BlobToROIConverter {
  protected:
    using ConvertFunc = void (*)(GstTensorMeta *, const GstStructure *, const GstStructure *,
                                 GstAnalyticsRelationMeta *);

    const std::string custom_postproc_lib;

  public:
    CustomToRoiConverter(BlobToMetaConverter::Initializer initializer, double confidence_threshold,
                         double iou_threshold, const std::string &custom_postproc_lib)
        : BlobToROIConverter(std::move(initializer), confidence_threshold, true, iou_threshold),
          custom_postproc_lib(custom_postproc_lib) {
    }

    TensorsTable convert(const OutputBlobs &output_blobs) override;

    static std::string getName() {
        return "custom_to_roi";
    }
};

} // namespace post_processing
