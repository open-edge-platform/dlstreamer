/*******************************************************************************
 * Copyright (C) 2018-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef _GST_GVA_WATERMARK_IMPL_H_
#define _GST_GVA_WATERMARK_IMPL_H_

#include "inference_backend/image_inference.h"
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <memory>

#ifndef _MSC_VER
#include <dlstreamer/gst/context.h>
#include <dlstreamer/vaapi/context.h>
#include <opencv2/core/va_intel.hpp>
#endif

#include <opencv2/core.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/imgproc.hpp>

G_BEGIN_DECLS

#define GST_TYPE_GVA_WATERMARK_IMPL (gst_gva_watermark_impl_get_type())
#define GST_GVA_WATERMARK_IMPL(obj)                                                                                    \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GVA_WATERMARK_IMPL, GstGvaWatermarkImpl))
#define GST_GVA_WATERMARK_IMPL_CLASS(klass)                                                                            \
    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GVA_WATERMARK_IMPL, GstGvaWatermarkImplClass))
#define GST_IS_GVA_WATERMARK_IMPL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GVA_WATERMARK_IMPL))
#define GST_IS_GVA_WATERMARK_IMPL_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GVA_WATERMARK_IMPL))

typedef struct _GstGvaWatermarkImpl GstGvaWatermarkImpl;
typedef struct _GstGvaWatermarkImplClass GstGvaWatermarkImplClass;

struct _GstGvaWatermarkImpl {
    GstBaseTransform base_transform;
    GstVideoInfo info;
    gchar *device;
    gchar *displ_cfg;
    bool obb;
    bool displ_avgfps;
    std::shared_ptr<struct Impl> impl;
    InferenceBackend::MemoryType negotiated_mem_type = InferenceBackend::MemoryType::ANY;

#ifndef _MSC_VER
    VADisplay va_dpy = nullptr;
    std::shared_ptr<dlstreamer::GSTContext> gst_ctx;
    std::shared_ptr<dlstreamer::VAAPIContext> vaapi_ctx;
    std::shared_ptr<dlstreamer::MemoryMapperGSTToVAAPI> gst_to_vaapi;
#endif

    bool overlay_ready = false;
    cv::Mat overlay_cpu;
    cv::UMat overlay_gpu;
};

struct _GstGvaWatermarkImplClass {
    GstBaseTransformClass base_gvawatermark_class;
};

GType gst_gva_watermark_impl_get_type(void);

enum { PROP_0, PROP_DEVICE, PROP_OBB, PROP_DISPL_AVGFPS, PROP_DISPL_CFG };

G_END_DECLS

#endif
