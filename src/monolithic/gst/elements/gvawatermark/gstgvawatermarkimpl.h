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

#ifndef _WIN32
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

#ifndef _WIN32
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

#define DISPL_AVGFPS_DESCRIPTION                                                                                       \
    "If true, display the average FPS read from gvafpscounter element on the output video, (default false)\n"          \
    "\t\t\tThe gvafpscounter element must be present in the pipeline.\n"                                               \
    "\t\t\te.g.: ... ! gvawatermark displ-avgfps=true ! gvafpscounter ! ..."

#define DISPL_CFG_DESCRIPTION                                                                                          \
    "Comma separated list of KEY=VALUE parameters of displayed notations.\n"                                           \
    "\t\t\tAvailable options: \n"                                                                                      \
    "\t\t\tshow-labels=<bool> enable or disable displaying text labels, default true\n"                                \
    "\t\t\ttext-scale=<double 0.1 to 2.0> scale factor for text labels, default 1.0\n"                                 \
    "\t\t\tthickness=<uint 1 to 10> bounding box thickness, default 2\n"                                               \
    "\t\t\tcolor-idx=<int> color index for bounding box, keypoints, and text, default -1 (use default colors: 0 red, " \
    "1 green, 2 blue)\n"                                                                                               \
    "\t\t\tdraw-txt-bg=<bool> enable or disable displaying text labels background, by enabling it the text color "     \
    "is set to white, default false\n"                                                                                 \
    "\t\t\te.g.: displ-cfg=show-labels=false\n"                                                                        \
    "\t\t\te.g.: displ-cfg=text-scale=0.5,thickness=3,color-idx=2"

G_END_DECLS

#endif
