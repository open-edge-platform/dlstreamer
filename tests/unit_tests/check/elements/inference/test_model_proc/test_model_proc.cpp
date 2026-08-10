/*******************************************************************************
 * Copyright (C) 2021-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "element_test_base.h"

#include <gst/video/video.h>
#include <gtest/gtest-spi.h>

#include "gva_base_inference.h"
#include "inference_impl.h"
#include "test_common.h"
#include "test_env.h"

class ModelProcTests : public ElementTest {
  public:
    ModelProcTests() {
        _srcCaps = GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("{ BGRA }"));
        _sinkCaps = GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("{ BGRA }"));
        _elementName = "gvaclassify";

        _model_path = TestEnv::getModelPath("colorcls2", "FP32");
        _model_proc_path = "model_proc_test_files/color_7_types_default.json";
    }

    void testModelProcLabels(const std::string &model_proc_path, const std::string &labels_str,
                             const std::map<std::string, std::vector<std::string>> &expected_labels) {
        setProperty("model", _model_path);
        if (!model_proc_path.empty())
            setProperty("model-proc", model_proc_path);
        if (!labels_str.empty())
            setProperty("labels", labels_str);

        setState(GST_STATE_PLAYING);
        GstCaps *caps = gst_caps_from_string("video/x-raw,format=BGRA,framerate=25/1");
        gst_caps_set_simple(caps, "width", G_TYPE_INT, 100, "height", G_TYPE_INT, 100, nullptr);
        setSrcCaps(caps);
        std::string bus_error;
        ASSERT_FALSE(hasErrorOnBus(bus_error)) << "got error on bus: " << bus_error;

        // Note: casting pointer unconditionally, since GVA_BASE_INFERENCE() macro will try to re-register
        // gobject type and cause cannot register existing type 'GvaBaseInference' error
        GvaBaseInference *base_inference = (GvaBaseInference *)_element;

        auto initializer = base_inference->post_proc->get_initializer();
        ASSERT_EQ(initializer.labels, expected_labels);
    }

  protected:
    std::string _model_path;
    std::string _model_proc_path;

    static const std::string mp_labels_array_and_path;
    static const std::string mp_labels_array_path;
    static const std::string mp_labels_wrong_path;
    static const std::string color_labels_path;
};

const std::string ModelProcTests::mp_labels_array_and_path = "model_proc_test_files/mp_labels_array_and_path.json";
const std::string ModelProcTests::mp_labels_array_path = "model_proc_test_files/mp_labels_array.json";
const std::string ModelProcTests::mp_labels_wrong_path = "model_proc_test_files/mp_labels_wrong_path.json";
const std::string ModelProcTests::color_labels_path = "model_proc_test_files/color_labels.txt";

TEST_F(ModelProcTests, modelProcLabelsArray) {
    const std::map<std::string, std::vector<std::string>> expected_labels{
        {"output", {"white", "gray", "yellow", "red", "green", "blue", "black"}}};

    testModelProcLabels(_model_proc_path, {}, expected_labels);
}

TEST_F(ModelProcTests, modelProcLabelsPath) {
    const std::map<std::string, std::vector<std::string>> expected_labels{
        {"output", {"pink", "cyan", "brown", "purple"}}};

    testModelProcLabels(mp_labels_array_and_path, {}, expected_labels);
}

TEST_F(ModelProcTests, emptyModelProcLabels) {
    const std::map<std::string, std::vector<std::string>> expected_labels{{"ANY", {"pink", "cyan", "brown", "purple"}}};

    testModelProcLabels({}, color_labels_path, expected_labels);
}

TEST_F(ModelProcTests, modelProcOverrideLabelsSingleLayer) {
    const std::map<std::string, std::vector<std::string>> expected_labels{
        {"output", {"pink", "cyan", "brown", "purple"}}};

    testModelProcLabels(mp_labels_array_path, color_labels_path, expected_labels);
}

TEST_F(ModelProcTests, modelProcOverrideLabelsByLayerName) {
    const std::string labels_str = "output=" + color_labels_path;
    const std::map<std::string, std::vector<std::string>> expected_labels{
        {"output", {"pink", "cyan", "brown", "purple"}}};

    testModelProcLabels(_model_proc_path, labels_str, expected_labels);
}

TEST_F(ModelProcTests, throwWithWrongLabelsPathInModelProc) {
    // Adapted from EXPECT_FATAL_FAILURE to run non-static method
    do {
        ::testing::TestPartResultArray gtest_failures;
        ::testing::internal::SingleFailureChecker gtest_checker(
            &gtest_failures, ::testing::TestPartResult::kFatalFailure, "got error on bus:");
        {
            ::testing::ScopedFakeTestPartResultReporter gtest_reporter(
                ::testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD, &gtest_failures);
            testModelProcLabels(mp_labels_wrong_path, {}, {});
        }
    } while (::testing::internal::AlwaysFalse());
}

TEST_F(ModelProcTests, throwWithWrongLabelsPathInProperty) {
    // Adapted from EXPECT_FATAL_FAILURE to run non-static method
    do {
        ::testing::TestPartResultArray gtest_failures;
        ::testing::internal::SingleFailureChecker gtest_checker(
            &gtest_failures, ::testing::TestPartResult::kFatalFailure, "got error on bus:");
        {
            ::testing::ScopedFakeTestPartResultReporter gtest_reporter(
                ::testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD, &gtest_failures);
            testModelProcLabels({}, "/non/existent/file", {});
        }
    } while (::testing::internal::AlwaysFalse());
}

// This class required in order to propagate fatal failures for subroutires of the test.
// Please refer to "Propagating Fatal Failures" in GTest documentation
class ThrowListener : public testing::EmptyTestEventListener {
    void OnTestPartResult(const testing::TestPartResult &result) override {
        if (result.type() == testing::TestPartResult::kFatalFailure) {
            throw testing::AssertionException(result);
        }
    }
};

GTEST_API_ int main(int argc, char **argv) {
    std::cout << "Running elements test " << argv[0] << std::endl;
    testing::InitGoogleTest(&argc, argv);
    // Initialize GStreamer
    gst_init(&argc, &argv);
    // Add listener
    testing::UnitTest::GetInstance()->listeners().Append(new ThrowListener);
    return RUN_ALL_TESTS();
}
