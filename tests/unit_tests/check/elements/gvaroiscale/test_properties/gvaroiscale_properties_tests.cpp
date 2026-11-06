/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/* TEST
 * Overview: Tests gvaroiscale property defaults, types, ranges, and read/write behavior.
 */

#include "gstgvaroiscale.h"

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

class GvaRoiScalePropertiesTest : public ::testing::Test {
  protected:
    void SetUp() override {
        element = G_OBJECT(g_object_new(GST_TYPE_GVA_ROI_SCALE, nullptr));
        ASSERT_NE(element, nullptr);
    }

    void TearDown() override {
        if (element)
            g_object_unref(element);
    }

    GObject *element = nullptr;
};

/* TEST
 * Tests gvaroiscale defaults, GObject types/access flags, and numeric property bounds.
 * Input: a newly constructed ROI scaler with no property overrides.
 * Values: default output=448x448, threshold=0, null output-dir, and empty target-classes;
 *         dimensions range [1,8192], confidence [0,1].
 * Expected: getters return defaults and all five properties are registered as read/write with the expected types.
 */
TEST_F(GvaRoiScalePropertiesTest, ExposesExpectedDefaultsAndPropertySpecs) {
    guint out_width = 0;
    guint out_height = 0;
    gdouble confidence_threshold = -1.0;
    gchar *output_dir = nullptr;
    gchar *target_classes = nullptr;
    g_object_get(element, "out-width", &out_width, "out-height", &out_height, "confidence-threshold",
                 &confidence_threshold, "output-dir", &output_dir, "target-classes", &target_classes, nullptr);

    EXPECT_EQ(out_width, 448u);
    EXPECT_EQ(out_height, 448u);
    EXPECT_DOUBLE_EQ(confidence_threshold, 0.0);
    EXPECT_EQ(output_dir, nullptr);
    EXPECT_STREQ(target_classes, "");
    g_free(output_dir);
    g_free(target_classes);

    expect_readwrite_property(element, "out-width", G_TYPE_UINT);
    expect_readwrite_property(element, "out-height", G_TYPE_UINT);
    expect_readwrite_property(element, "confidence-threshold", G_TYPE_DOUBLE);
    expect_readwrite_property(element, "output-dir", G_TYPE_STRING);
    expect_readwrite_property(element, "target-classes", G_TYPE_STRING);

    expect_uint_range(element, "out-width", 1, 8192);
    expect_uint_range(element, "out-height", 1, 8192);
    expect_double_range(element, "confidence-threshold", 0.0, 1.0);
}

/* TEST
 * Tests setting and reading back all gvaroiscale properties.
 * Input: output size 640x360, threshold 0.75, output path, and classes person,vehicle.
 * Values: dimensions and confidence are inside their declared ranges.
 * Expected: getters return each assigned value unchanged.
 */
TEST_F(GvaRoiScalePropertiesTest, SetPropertiesCanBeReadBack) {
    g_object_set(element, "out-width", 640u, "out-height", 360u, "confidence-threshold", 0.75,
                 "output-dir", "/tmp/gva-scaled-rois", "target-classes", "person,vehicle", nullptr);

    guint out_width = 0;
    guint out_height = 0;
    gdouble confidence_threshold = 0.0;
    gchar *output_dir = nullptr;
    gchar *target_classes = nullptr;
    g_object_get(element, "out-width", &out_width, "out-height", &out_height, "confidence-threshold",
                 &confidence_threshold, "output-dir", &output_dir, "target-classes", &target_classes, nullptr);

    EXPECT_EQ(out_width, 640u);
    EXPECT_EQ(out_height, 360u);
    EXPECT_DOUBLE_EQ(confidence_threshold, 0.75);
    EXPECT_STREQ(output_dir, "/tmp/gva-scaled-rois");
    EXPECT_STREQ(target_classes, "person,vehicle");
    g_free(output_dir);
    g_free(target_classes);
}

} // namespace

int main(int argc, char **argv) {
    // Initialize GStreamer before constructing elements, then initialize GoogleTest before running the tests.
    gst_init(&argc, &argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}