/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#pragma once

#include "blob_to_meta_converter.h"
#include "post_proc_common.h"

#include <gst/analytics/analytics.h>
#include <gst/gst.h>
#include <gst/video/gstvideometa.h>

namespace post_processing {

class MetaAttacher {
  public:
    MetaAttacher() = default;

    virtual void attach(const TensorsTable &tensors_batch, FramesWrapper &frames,
                        const BlobToMetaConverter &blob_to_meta) = 0;

    using Ptr = std::unique_ptr<MetaAttacher>;
    static Ptr create(ConverterType converter_type, AttachType attach_type);

    virtual ~MetaAttacher() = default;
};

class ROIToFrameAttacher : public MetaAttacher {
  public:
    ROIToFrameAttacher() = default;

    void attach(const TensorsTable &tensors_batch, FramesWrapper &frames,
                const BlobToMetaConverter &blob_to_meta) override;
};

class TensorToFrameAttacher : public MetaAttacher {
  public:
    TensorToFrameAttacher() = default;

    void attach(const TensorsTable &tensors_batch, FramesWrapper &frames,
                const BlobToMetaConverter &blob_to_meta) override;
};

class TensorToROIAttacher : public MetaAttacher {
  private:
    GstVideoRegionOfInterestMeta *findROIMeta(GstBuffer *buffer, GstVideoRegionOfInterestMeta *frame_roi);
    bool findODMeta(GstBuffer *buffer, GstVideoRegionOfInterestMeta *frame_roi, GstAnalyticsODMtd *rlt_mtd);

  public:
    TensorToROIAttacher() = default;

    void attach(const TensorsTable &tensors_batch, FramesWrapper &frames,
                const BlobToMetaConverter &blob_to_meta) override;
};

class FrameToExistingROIsTensorAttacher : public MetaAttacher {
  private:
    GstVideoRegionOfInterestMeta *findROIMeta(GstBuffer *buffer, gint roi_id);

  public:
    FrameToExistingROIsTensorAttacher() = default;

    void attach(const TensorsTable &tensors_batch, FramesWrapper &frames,
                const BlobToMetaConverter &blob_to_meta) override;
};

class TensorToFrameAttacherForMicro : public MetaAttacher {
  private:
    GstVideoRegionOfInterestMeta *findROIMeta(GstBuffer *buffer, const GstVideoRegionOfInterestMeta &frame_roi);

  public:
    TensorToFrameAttacherForMicro() = default;

    void attach(const TensorsTable &tensors_batch, FramesWrapper &frames,
                const BlobToMetaConverter &blob_to_meta) override;
};

} // namespace post_processing
