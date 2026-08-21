/*******************************************************************************
 * Copyright (C) 2018-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "common.h"

#include "gstgvadetect.h"
#include "gva_base_inference.h"
#include "inference_impl.h"

#include <gst/check/gstcheck.h>

void check_model_input_info(gchar *pipeline_str, const int expected_width, const int expected_height,
                            const int expected_batch_size) {
    GstElement *pipeline = gst_parse_launch(pipeline_str, NULL);
    ck_assert(pipeline != NULL);

    GstMessage *msg = NULL;
    GstBus *bus = gst_element_get_bus(pipeline);
    ck_assert(bus != NULL);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    msg = gst_bus_timed_pop_filtered(bus, GST_SECOND, (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    ck_assert(msg == NULL || GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS);
    if (msg)
        gst_message_unref(msg);

    GstGvaDetect *gvadetect = reinterpret_cast<GstGvaDetect *>(gst_bin_get_by_name(GST_BIN(pipeline), "gvadetect"));
    GvaBaseInference *base_inference = reinterpret_cast<GvaBaseInference *>(gvadetect);

    InferenceCoordinator::Model model = base_inference->inference->GetModel();
    InferenceBackend::ImageInference *openvino_infer = model.inference.get();
    fail_unless(openvino_infer);

    size_t width, height, batch_size;
    int format;
    int mem_type;
    openvino_infer->GetModelImageInputInfo(width, height, batch_size, format, mem_type);
    ck_assert_msg(width == expected_width, "Width of the model input layer does not match the expected value");
    ck_assert_msg(height == expected_height, "Height of the model input layer does not match the expected value");
    ck_assert_msg(batch_size == expected_batch_size,
                  "Batch-size of the model input layer does not match the expected value");

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(gvadetect);
    gst_object_unref(pipeline);
}
