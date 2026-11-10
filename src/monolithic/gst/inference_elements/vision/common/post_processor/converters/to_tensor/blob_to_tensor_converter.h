/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include <gst/gst.h>

#include "post_processor/blob_to_meta_converter.h"
#include "post_processor/post_proc_common.h"

#include <memory>
#include <string>

namespace post_processing {

const std::string DEFAULT_ANOMALY_DETECTION_TASK = "classification";

class BlobToTensorConverter : public BlobToMetaConverter {
  protected:
    GVA::Tensor createTensor() const;

  public:
    BlobToTensorConverter(BlobToMetaConverter::Initializer initializer);

    virtual TensorsTable convert(const OutputBlobs &output_blobs) = 0;

    static BlobToMetaConverter::Ptr create(BlobToMetaConverter::Initializer initializer,
                                           const std::string &converter_name, const std::string &custom_postproc_lib);
};

} // namespace post_processing
