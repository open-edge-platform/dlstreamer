/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* TEST
 * Overview: Tests gvaframeselect property defaults, types, ranges, and read/write behavior.
 */

#include "gstgvaframeselect.h"

#include <gst/gst.h>
#include <gtest/gtest.h>

namespace {

void expect_readwrite_property(GObject *object, const gchar *name, GType value_type) {
    GParamSpec *property = g_object_class_find_property(G_OBJECT_GET_CLASS(object), name);
    ASSERT_NE(property, nullptr) << name;
    EXPECT_EQ(G_PARAM_SPEC_VALUE_TYPE(property), value_type) << name;
    EXPECT_EQ(property->flags & G_PARAM_READWRITE, G_PARAM_READWRITE) << name;
}

void expect_uint_range(GObject *object, const gchar *name, guint minimum, guint maximum) {
    GParamSpec *property = g_object_class_find_property(G_OBJECT_GET_CLASS(object), name);
    ASSERT_NE(property, nullptr) << name;
    ASSERT_TRUE(G_IS_PARAM_SPEC_UINT(property)) << name;
    EXPECT_EQ(G_PARAM_SPEC_UINT(property)->minimum, minimum) << name;
    EXPECT_EQ(G_PARAM_SPEC_UINT(property)->maximum, maximum) << name;
}

void expect_double_range(GObject *object, const gchar *name, gdouble minimum, gdouble maximum) {
    GParamSpec *property = g_object_class_find_property(G_OBJECT_GET_CLASS(object), name);
    ASSERT_NE(property, nullptr) << name;
    ASSERT_TRUE(G_IS_PARAM_SPEC_DOUBLE(property)) << name;
    EXPECT_DOUBLE_EQ(G_PARAM_SPEC_DOUBLE(property)->minimum, minimum) << name;
    EXPECT_DOUBLE_EQ(G_PARAM_SPEC_DOUBLE(property)->maximum, maximum) << name;
}

class GvaFrameSelectPropertiesTest : public ::testing::Test {
  protected:
    void SetUp() override {
        element = G_OBJECT(g_object_new(GST_TYPE_GVA_FRAME_SELECT, nullptr));
        ASSERT_NE(element, nullptr);
    }

    void TearDown() override {
        if (element)
            g_object_unref(element);
    }

    GObject *element = nullptr;
};

/* TEST
 * Tests gvaframeselect defaults, GObject types/access flags, and numeric property bounds.
 * Input: a newly constructed frame selector with no property overrides.
 * Values: defaults are 900 sub-stream frames, budget 8, person,vehicle, threshold 0.5, padding 0, null output-dir.
 * Expected: getters return defaults; frame count/budget ranges start at 1, confidence is [0,1],
 *           and padding starts at 0.
 */
TEST_F(GvaFrameSelectPropertiesTest, ExposesExpectedDefaultsAndPropertySpecs) {
    guint sub_stream_frames = 0;
    guint frame_budget = 0;
    gchar *target_classes = nullptr;
    gdouble confidence_threshold = 0.0;
    guint roi_padding = 0;
    gchar *output_dir = nullptr;
    g_object_get(element, "sub-stream-frames", &sub_stream_frames, "frame-budget", &frame_budget, "target-classes",
                 &target_classes, "confidence-threshold", &confidence_threshold, "roi-padding", &roi_padding,
                 "output-dir", &output_dir, nullptr);

    EXPECT_EQ(sub_stream_frames, 900u);
    EXPECT_EQ(frame_budget, 8u);
    EXPECT_STREQ(target_classes, "person,vehicle");
    EXPECT_DOUBLE_EQ(confidence_threshold, 0.5);
    EXPECT_EQ(roi_padding, 0u);
    EXPECT_EQ(output_dir, nullptr);
    g_free(target_classes);
    g_free(output_dir);

    expect_readwrite_property(element, "sub-stream-frames", G_TYPE_UINT);
    expect_readwrite_property(element, "frame-budget", G_TYPE_UINT);
    expect_readwrite_property(element, "target-classes", G_TYPE_STRING);
    expect_readwrite_property(element, "confidence-threshold", G_TYPE_DOUBLE);
    expect_readwrite_property(element, "roi-padding", G_TYPE_UINT);
    expect_readwrite_property(element, "output-dir", G_TYPE_STRING);

    expect_uint_range(element, "sub-stream-frames", 1, G_MAXUINT);
    expect_uint_range(element, "frame-budget", 1, G_MAXUINT);
    expect_double_range(element, "confidence-threshold", 0.0, 1.0);
    expect_uint_range(element, "roi-padding", 0, G_MAXUINT);
}

/* TEST
 * Tests setting and reading back all gvaframeselect properties.
 * Input: 120-frame block, budget 6, classes car,person, threshold 0.8, padding 12, and an output path.
 * Values: assigned numeric values are inside the declared ranges.
 * Expected: getters return each assigned value unchanged.
 */
TEST_F(GvaFrameSelectPropertiesTest, SetPropertiesCanBeReadBack) {
    g_object_set(element, "sub-stream-frames", 120u, "frame-budget", 6u, "target-classes", "car,person",
                 "confidence-threshold", 0.8, "roi-padding", 12u, "output-dir", "/tmp/gva-selected-frames", nullptr);

    guint sub_stream_frames = 0;
    guint frame_budget = 0;
    gchar *target_classes = nullptr;
    gdouble confidence_threshold = 0.0;
    guint roi_padding = 0;
    gchar *output_dir = nullptr;
    g_object_get(element, "sub-stream-frames", &sub_stream_frames, "frame-budget", &frame_budget, "target-classes",
                 &target_classes, "confidence-threshold", &confidence_threshold, "roi-padding", &roi_padding,
                 "output-dir", &output_dir, nullptr);

    EXPECT_EQ(sub_stream_frames, 120u);
    EXPECT_EQ(frame_budget, 6u);
    EXPECT_STREQ(target_classes, "car,person");
    EXPECT_DOUBLE_EQ(confidence_threshold, 0.8);
    EXPECT_EQ(roi_padding, 12u);
    EXPECT_STREQ(output_dir, "/tmp/gva-selected-frames");
    g_free(target_classes);
    g_free(output_dir);
}

} // namespace

int main(int argc, char **argv) {
    // Initialize GStreamer before constructing elements, then initialize GoogleTest before running the tests.
    gst_init(&argc, &argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}