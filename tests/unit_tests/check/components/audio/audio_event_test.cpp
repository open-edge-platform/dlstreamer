/*******************************************************************************
 * Copyright (C) 2020-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "audio_event.h"
#include <gtest/gtest.h>

struct AudioEventTest : public ::testing::Test {

    GstBuffer *buffer;
    GVA::AudioEvent *event;
    GQuark event_type = 0;
    guint64 start_timestamp = 2;
    guint64 end_timestamp = 3;

    void SetUp() {
        buffer = gst_buffer_new_and_alloc(0);

        GstGVAAudioEventMeta *meta =
            gst_gva_buffer_add_audio_event_meta_id(buffer, event_type, start_timestamp, end_timestamp);
        event = new GVA::AudioEvent(meta);
    }

    void TearDown() {
        if (buffer)
            gst_buffer_unref(buffer);
        if (event)
            delete event;
    }
};

TEST_F(AudioEventTest, AudioEventTestSegment) {
    GVA::Segment<guint64> seg = event->segment();
    ASSERT_EQ(seg.start, start_timestamp);
    ASSERT_EQ(seg.end, end_timestamp);
}

TEST_F(AudioEventTest, AudioEventTestLabel) {
    const constexpr char target_label[] = "new_label";
    ASSERT_EQ(event->label(), "");
    event->set_label(target_label);
    ASSERT_EQ(event->label(), target_label);
}

TEST_F(AudioEventTest, AudioEventTestConfidence) {
    ASSERT_EQ(event->confidence(), 0.0);
}

TEST_F(AudioEventTest, AudioEventTestTensors) {
    ASSERT_EQ(event->tensors().size(), 0);

    const constexpr size_t tensor_meta_size = 10;
    const constexpr char field_name[] = "model_name";
    const constexpr char model_name[] = "test_model";
    for (size_t i = 0; i < tensor_meta_size; ++i) {
        GVA::Tensor tensor = event->add_tensor("test" + std::to_string(i));
        std::string test_model = model_name + std::to_string(i);
        gst_structure_set(tensor.gst_structure(), field_name, G_TYPE_STRING, test_model.c_str(), NULL);
    }

    std::vector<GVA::Tensor> frame_tensors = event->tensors();
    ASSERT_EQ(frame_tensors.size(), tensor_meta_size);
}

TEST_F(AudioEventTest, AudioEventTestDetection) {
    GVA::Tensor tensor = event->detection();
    ASSERT_EQ(tensor.name(), "detection");
}

TEST_F(AudioEventTest, AudioEventTestLabel_id) {
    ASSERT_EQ(event->label_id(), 0);
    GVA::Tensor tensor = event->detection();
    ASSERT_EQ(event->label_id(), 0);
}

TEST_F(AudioEventTest, AudioEventTestMeta) {
    GstGVAAudioEventMeta *meta = event->_meta();
    ASSERT_EQ(meta->start_timestamp, start_timestamp);
    ASSERT_EQ(meta->end_timestamp, end_timestamp);
    ASSERT_EQ(meta->event_type, event_type);
}
