/*******************************************************************************
 * Copyright (C) 2018-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "gstgvaaudiodetect.h"
#include "post_processors.h"
#include "pre_processors.h"
#include "utils.h"

#define ELEMENT_LONG_NAME "Audio event detection based on input audio"
#define ELEMENT_DESCRIPTION "Performs audio event detection using AclNet model."

enum { PROP_0 };

static GstStaticPadTemplate sink_factory =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS(AUDIO_CAPS));

static GstStaticPadTemplate src_factory =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS(AUDIO_CAPS));

GST_DEBUG_CATEGORY_STATIC(gva_audio_detect_debug_category);
#define GST_CAT_DEFAULT gva_audio_detect_debug_category

G_DEFINE_TYPE_WITH_CODE(GvaAudioDetect, gst_gva_audio_detect, GST_TYPE_GVA_AUDIO_BASE_INFERENCE,
                        GST_DEBUG_CATEGORY_INIT(gva_audio_detect_debug_category, "gvaaudiodetect", 0,
                                                "debug category for gvaaudiodetect element"));

static void gst_gva_audio_detect_finalize(GObject *object);

void gst_gva_audio_detect_class_init(GvaAudioDetectClass *gvaaudiodetect_class) {

    GstElementClass *element_class = GST_ELEMENT_CLASS(gvaaudiodetect_class);

    GObjectClass *gobject_class = G_OBJECT_CLASS(gvaaudiodetect_class);

    gobject_class->finalize = gst_gva_audio_detect_finalize;

    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_factory));
    gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_factory));

    gst_element_class_set_static_metadata(element_class, ELEMENT_LONG_NAME, "Audio EVent detection",
                                          ELEMENT_DESCRIPTION, "Intel Corporation");
}

void gst_gva_audio_detect_init(GvaAudioDetect *gvaaudiodetect) {
    GST_DEBUG_OBJECT(gvaaudiodetect, "gst_gva_audio_detect_init");
    if (gvaaudiodetect == NULL)
        return;

    GST_DEBUG_OBJECT(gvaaudiodetect, "%s", GST_ELEMENT_NAME(GST_ELEMENT(gvaaudiodetect)));

    gvaaudiodetect->audio_base_inference.pre_proc = GET_NORMALIZED_SAMPLES;
    gvaaudiodetect->audio_base_inference.post_proc = EXTRACT_RESULTS;
    gvaaudiodetect->audio_base_inference.req_sample_size = GET_NUM_OF_SAMPLES_REQUIRED;
    gvaaudiodetect->req_num_samples = SAMPLE_AUDIO_RATE;
}

void gst_gva_audio_detect_finalize(GObject *object) {
    GvaAudioDetect *gvaaudiodetect = GVA_AUDIO_DETECT(object);

    GST_DEBUG_OBJECT(gvaaudiodetect, "finalize");
    gvaaudiodetect->audio_base_inference.pre_proc = NULL;
    gvaaudiodetect->audio_base_inference.post_proc = NULL;
    gvaaudiodetect->audio_base_inference.req_sample_size = NULL;

    G_OBJECT_CLASS(gst_gva_audio_detect_parent_class)->finalize(object);
}