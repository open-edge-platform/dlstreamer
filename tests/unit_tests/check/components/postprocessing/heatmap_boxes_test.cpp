/*******************************************************************************
 * Copyright (C) 2023-2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "common/post_processor/converters/to_roi/heatmap_boxes.h"
#include <dlstreamer/gst/dictionary.h>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace InferenceBackend;
using namespace post_processing;

class TestOutputBlob : public OutputBlob {
    std::unique_ptr<uint8_t[]> _data;
    uintmax_t _data_size = 0;
    std::vector<size_t> _dims;

  public:
    // Loads raw blob from file
    TestOutputBlob(std::string_view filename) {
        _data_size = std::filesystem::file_size(filename);
        _data = std::make_unique<uint8_t[]>(_data_size);

        std::ifstream f(filename.data(), std::ios::binary);
        f.read(reinterpret_cast<char *>(_data.get()), _data_size);
        if (!f)
            throw std::runtime_error(std::string("Error reading file ") + filename.data() +
                                     ". Read bytes: " + std::to_string(f.gcount()));
        f.close();
    }

    const std::vector<size_t> &GetDims() const {
        return _dims;
    }

    void SetDims(std::vector<size_t> dims) {
        _dims = std::move(dims);
    }

    const void *GetData() const override {
        return _data.get();
    }

    Layout GetLayout() const override {
        return Layout::NCHW;
    }

    Precision GetPrecision() const override {
        return Precision::FP32;
    }
};

struct HeatMapBoxesConverterTest : public testing::Test {
  protected:
    double _confidence_threshold{0.5};
    std::vector<size_t> _output_dims{1, 2, 1024, 1824};

    // Structure with parameters from model-proc and dictionary for ease of access
    GstStructure *_gst_structure{nullptr};
    dlstreamer::GSTDictionary _model_proc_params{nullptr};

    BlobToMetaConverter::Initializer CreateInitializer() {
        BlobToMetaConverter::Initializer initializer;
        initializer.model_name = "heatmap_boxes_test";
        initializer.outputs_info = {{"layer_name", _output_dims}};
        initializer.input_image_info.batch_size = 1;
        initializer.input_image_info.width = 1824;
        initializer.input_image_info.height = 1024;
        // This structure gets freed only at TearDown!
        initializer.model_proc_output_info = GstStructureUniquePtr(_gst_structure, [](auto) {});
        return initializer;
    }

    void SetUp() override {
        _gst_structure = gst_structure_new_empty("ANY");
        _model_proc_params = dlstreamer::GSTDictionary(_gst_structure);
        // Add confidence_threshold by default
        _model_proc_params.set("confidence_threshold", _confidence_threshold);
    }

    void TearDown() override {
        if (_gst_structure)
            gst_structure_free(_gst_structure);
        _gst_structure = nullptr;
    }

    std::shared_ptr<TestOutputBlob> GetTestBlob() {
        static constexpr auto test_file = "postprocessing_test_files/data_1.bin";
        return std::make_shared<TestOutputBlob>(test_file);
    }
};

TEST_F(HeatMapBoxesConverterTest, ConverterName) {
    ASSERT_EQ(HeatMapBoxesConverter::getName(), "heatmap_boxes");
}

TEST_F(HeatMapBoxesConverterTest, InvalidParameterInModelProc) {
    _model_proc_params.set("minimum_side", -1.0);
    EXPECT_THROW(HeatMapBoxesConverter(CreateInitializer(), _confidence_threshold), std::invalid_argument);
    _model_proc_params.set("binarize_threshold", 256.0);
    EXPECT_THROW(HeatMapBoxesConverter(CreateInitializer(), _confidence_threshold), std::invalid_argument);
}

TEST_F(HeatMapBoxesConverterTest, CanCovert) {
    HeatMapBoxesConverter post_proc(CreateInitializer(), _confidence_threshold);
    auto blob = GetTestBlob();
    blob->SetDims(_output_dims);

    OutputBlobs blobs_map{{"output_layer_name", blob}};
    TensorsTable result = post_proc.convert(blobs_map);

    SCOPED_TRACE(::testing::Message() << "Number of boxes: " << result.size());
    ASSERT_FALSE(result.empty());
    // Test binary file contains single batch
    for (const GstStructure *bbox : result[0][0]) {
        EXPECT_TRUE(gst_structure_has_field(bbox, "x_min"));
        EXPECT_TRUE(gst_structure_has_field(bbox, "x_max"));
        EXPECT_TRUE(gst_structure_has_field(bbox, "y_min"));
        EXPECT_TRUE(gst_structure_has_field(bbox, "y_max"));
        EXPECT_TRUE(gst_structure_has_field(bbox, "confidence"));
        double x_min, y_min, x_max, y_max, confidence;
        gst_structure_get_double(bbox, "x_min", &x_min);
        gst_structure_get_double(bbox, "x_max", &x_max);
        gst_structure_get_double(bbox, "y_min", &y_min);
        gst_structure_get_double(bbox, "y_max", &y_max);
        gst_structure_get_double(bbox, "confidence", &confidence);
        ASSERT_NEAR(x_min, 0.05427, 0.0001);
        ASSERT_NEAR(x_max, 0.08552, 0.0001);
        ASSERT_NEAR(y_min, 0.37890, 0.0001);
        ASSERT_NEAR(y_max, 0.39843, 0.0001);
        ASSERT_NEAR(confidence, 0.88393, 0.0001);
    }
}
