/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

/**
 * @file test_gvagenai_metrics_message.cpp
 * @brief Unit tests for the "dlstreamer/genai-request" bus message and the post-metrics property.
 *
 * The structure is the contract with applications, so its name, version, field names and field types are
 * checked exactly. Posting itself needs a model and is not covered here.
 */

#include "gstgvagenai.h"
#include "metrics_message.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <utility>

namespace {

genai::GenAIRequestMetrics sample_metrics() {
    genai::GenAIRequestMetrics m;
    m.ttft_ms = 14156.43;
    m.tpot_ms = 52.5;
    m.generate_duration_ms = 18073.39;
    m.inference_duration_ms = 17990.0;
    m.prepare_embeddings_duration_ms = 120.25;
    m.num_input_tokens = 1234;
    m.num_generated_tokens = 75;
    return m;
}

} // namespace

TEST(GenAIRequestMessage, NameAndVersion) {
    GstStructure *s = genai::create_request_metrics_structure(sample_metrics(), 4);
    EXPECT_STREQ(gst_structure_get_name(s), "dlstreamer/genai-request");
    guint version = 0;
    ASSERT_TRUE(gst_structure_get_uint(s, "version", &version));
    EXPECT_EQ(version, 1u);
    gst_structure_free(s);
}

TEST(GenAIRequestMessage, FieldsHaveDocumentedTypesAndValues) {
    GstStructure *s = genai::create_request_metrics_structure(sample_metrics(), 4);

    guint frames = 0;
    ASSERT_TRUE(gst_structure_get_uint(s, "frames", &frames));
    EXPECT_EQ(frames, 4u);

    const std::pair<const char *, double> doubles[] = {
        {"ttft_ms", 14156.43},
        {"tpot_ms", 52.5},
        {"generate_duration_ms", 18073.39},
        {"inference_duration_ms", 17990.0},
        {"prepare_embeddings_duration_ms", 120.25},
    };
    for (const auto &[name, expected] : doubles) {
        gdouble value = 0.0;
        ASSERT_TRUE(gst_structure_get_double(s, name, &value)) << name;
        EXPECT_DOUBLE_EQ(value, expected) << name;
    }

    guint64 tokens = 0;
    ASSERT_TRUE(gst_structure_get_uint64(s, "num_input_tokens", &tokens));
    EXPECT_EQ(tokens, 1234u);
    ASSERT_TRUE(gst_structure_get_uint64(s, "num_generated_tokens", &tokens));
    EXPECT_EQ(tokens, 75u);
    gst_structure_free(s);
}

TEST(GenAIRequestMessage, HasNoUndocumentedFields) {
    const std::set<std::string> documented = {"version",
                                              "frames",
                                              "ttft_ms",
                                              "tpot_ms",
                                              "generate_duration_ms",
                                              "inference_duration_ms",
                                              "prepare_embeddings_duration_ms",
                                              "num_input_tokens",
                                              "num_generated_tokens"};
    GstStructure *s = genai::create_request_metrics_structure(sample_metrics(), 1);
    std::set<std::string> fields;
    for (gint i = 0; i < gst_structure_n_fields(s); i++)
        fields.insert(gst_structure_nth_field_name(s, i));
    EXPECT_EQ(fields, documented);
    gst_structure_free(s);
}

TEST(GvaGenAIPostMetrics, DisabledByDefault) {
    gpointer element = g_object_ref_sink(g_object_new(gst_gvagenai_get_type(), NULL));
    gboolean post_metrics = TRUE;
    g_object_get(element, "post-metrics", &post_metrics, NULL);
    EXPECT_FALSE(post_metrics);
    gst_object_unref(element);
}

TEST(GvaGenAIPostMetrics, IndependentOfJsonMetrics) {
    gpointer element = g_object_ref_sink(g_object_new(gst_gvagenai_get_type(), NULL));
    gboolean post_metrics = FALSE, metrics = TRUE;

    g_object_set(element, "post-metrics", TRUE, NULL);
    g_object_get(element, "post-metrics", &post_metrics, "metrics", &metrics, NULL);
    EXPECT_TRUE(post_metrics);
    EXPECT_FALSE(metrics);

    g_object_set(element, "post-metrics", FALSE, "metrics", TRUE, NULL);
    g_object_get(element, "post-metrics", &post_metrics, "metrics", &metrics, NULL);
    EXPECT_FALSE(post_metrics);
    EXPECT_TRUE(metrics);
    gst_object_unref(element);
}

GTEST_API_ int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
