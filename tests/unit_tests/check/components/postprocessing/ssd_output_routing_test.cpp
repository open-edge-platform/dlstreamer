/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "common/post_processor/blob_to_meta_converter.h"
#include "common/post_processor/converters/to_roi/boxes_labels_scores.h"

#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace post_processing;
using namespace InferenceBackend;

namespace {

class OutputBlobStub : public OutputBlob {
    std::vector<uint8_t> data;
    std::vector<size_t> dims;
    Precision precision;

  public:
    template <typename T>
    OutputBlobStub(std::vector<size_t> shape, const std::vector<T> &values, Precision type)
        : data(values.size() * sizeof(T)), dims(std::move(shape)), precision(type) {
        std::memcpy(data.data(), values.data(), data.size());
    }

    const std::vector<size_t> &GetDims() const override {
        return dims;
    }
    Layout GetLayout() const override {
        return Layout::ANY;
    }
    Precision GetPrecision() const override {
        return precision;
    }
    const void *GetData() const override {
        return data.data();
    }
};

BlobToMetaConverter::Initializer makeSsdInitializer(ModelOutputsInfo outputs, size_t batch_size = 1,
                                                    const char *converter = "ssd") {
    BlobToMetaConverter::Initializer initializer;
    initializer.model_name = "generic_model";
    initializer.input_image_info = {640, 640, batch_size};
    initializer.outputs_info = std::move(outputs);
    initializer.labels = {"person", "vehicle"};
    initializer.model_proc_output_info =
        GstStructureUniquePtr(gst_structure_new("ANY", "converter", G_TYPE_STRING, converter, "confidence_threshold",
                                                G_TYPE_DOUBLE, 0.5, nullptr),
                              gst_structure_free);
    return initializer;
}

OutputBlobs makeDuplicateDetections() {
    return {
        {"bboxes", std::make_shared<OutputBlobStub>(std::vector<size_t>{1, 2, 4},
                                                    std::vector<float>{0.1f, 0.1f, 0.5f, 0.5f, 0.1f, 0.1f, 0.5f, 0.5f},
                                                    Blob::Precision::FP32)},
        {"labels",
         std::make_shared<OutputBlobStub>(std::vector<size_t>{1, 2}, std::vector<int64_t>{0, 0}, Blob::Precision::I64)},
        {"scores", std::make_shared<OutputBlobStub>(std::vector<size_t>{1, 2}, std::vector<float>{0.9f, 0.8f},
                                                    Blob::Precision::FP32)},
    };
}

const ModelOutputsInfo duplicate_detections_info = {{"bboxes", {1, 2, 4}}, {"labels", {1, 2}}, {"scores", {1, 2}}};

double detectionField(const GstStructure *tensor, const char *name) {
    double value = 0;
    EXPECT_TRUE(gst_structure_get_double(tensor, name, &value));
    return value;
}

int labelId(const GstStructure *tensor) {
    int value = -1;
    EXPECT_TRUE(gst_structure_get_int(tensor, "label_id", &value));
    return value;
}

} // namespace

TEST(SsdOutputRoutingTest, ThreeOutputContractUsesSeparateScoresAndBatches) {
    ModelOutputsInfo outputs = {{"bboxes", {2, 2, 4}}, {"labels", {2, 2}}, {"scores", {2, 2}}};
    auto converter = BlobToMetaConverter::create(makeSsdInitializer(outputs, 2), ConverterType::TO_ROI, "ANY", "");
    OutputBlobs blobs = {
        {"bboxes", std::make_shared<OutputBlobStub>(std::vector<size_t>{2, 2, 4},
                                                    std::vector<float>{0.1f, 0.2f, 0.5f, 0.6f, 0.f, 0.f, 1.f, 1.f,
                                                                       0.25f, 0.3f, 0.75f, 0.8f, 0.f, 0.f, 1.f, 1.f},
                                                    Blob::Precision::FP32)},
        {"labels", std::make_shared<OutputBlobStub>(std::vector<size_t>{2, 2}, std::vector<int64_t>{0, 1, 1, 0},
                                                    Blob::Precision::I64)},
        {"scores", std::make_shared<OutputBlobStub>(std::vector<size_t>{2, 2},
                                                    std::vector<float>{0.9f, 0.1f, 0.8f, 0.2f}, Blob::Precision::FP32)},
    };

    auto result = converter->convert(blobs);
    ASSERT_EQ(result.size(), 2);
    ASSERT_EQ(result[0].size(), 1);
    ASSERT_EQ(result[1].size(), 1);
    EXPECT_NEAR(detectionField(result[0][0][0], "x_min"), 0.1, 1e-6);
    EXPECT_NEAR(detectionField(result[0][0][0], "confidence"), 0.9, 1e-6);
    EXPECT_NEAR(detectionField(result[1][0][0], "x_max"), 0.75, 1e-6);
    EXPECT_NEAR(detectionField(result[1][0][0], "confidence"), 0.8, 1e-6);
}

TEST(SsdOutputRoutingTest, OrdinarySsdUsesBoxesLabels) {
    ModelOutputsInfo outputs = {{"boxes", {1, 1, 5}}, {"labels", {1}}};
    auto converter = BlobToMetaConverter::create(makeSsdInitializer(outputs), ConverterType::TO_ROI, "ANY", "");
    OutputBlobs blobs = {
        {"boxes",
         std::make_shared<OutputBlobStub>(std::vector<size_t>{1, 1, 5},
                                          std::vector<float>{64.f, 128.f, 320.f, 384.f, 0.9f}, Blob::Precision::FP32)},
        {"labels",
         std::make_shared<OutputBlobStub>(std::vector<size_t>{1}, std::vector<int64_t>{0}, Blob::Precision::I64)},
    };
    auto result = converter->convert(blobs);
    ASSERT_EQ(result.size(), 1);
    ASSERT_EQ(result[0].size(), 1);
    EXPECT_NEAR(detectionField(result[0][0][0], "x_min"), 0.1, 1e-6);
    EXPECT_NEAR(detectionField(result[0][0][0], "confidence"), 0.9, 1e-6);
}

TEST(SsdOutputRoutingTest, ThreeOutputConverterRejectsMismatchedBlobs) {
    ModelOutputsInfo outputs = {{"bboxes", {1, 1, 4}}, {"labels", {1, 1}}, {"scores", {1, 1}}};
    auto converter = BlobToMetaConverter::create(makeSsdInitializer(outputs), ConverterType::TO_ROI, "ANY", "");
    OutputBlobs blobs = {
        {"bboxes", std::make_shared<OutputBlobStub>(std::vector<size_t>{1, 1, 4},
                                                    std::vector<float>{0.f, 0.f, 1.f, 1.f}, Blob::Precision::FP32)},
        {"labels",
         std::make_shared<OutputBlobStub>(std::vector<size_t>{1, 1}, std::vector<int64_t>{0}, Blob::Precision::I64)},
        {"scores", std::make_shared<OutputBlobStub>(std::vector<size_t>{1, 2}, std::vector<float>{0.9f, 0.1f},
                                                    Blob::Precision::FP32)},
    };
    EXPECT_THROW(converter->convert(blobs), std::runtime_error);
}

TEST(SsdOutputRoutingTest, LegacyBoxesLabelsNameReadsLabelsOfEachImage) {
    ModelOutputsInfo outputs = {{"boxes", {2, 1, 5}}, {"labels", {2, 1}}};
    auto converter =
        BlobToMetaConverter::create(makeSsdInitializer(outputs, 2, "boxes_labels"), ConverterType::TO_ROI, "ANY", "");
    OutputBlobs blobs = {
        {"boxes", std::make_shared<OutputBlobStub>(
                      std::vector<size_t>{2, 1, 5},
                      std::vector<float>{64.f, 64.f, 320.f, 320.f, 0.9f, 320.f, 320.f, 640.f, 640.f, 0.8f},
                      Blob::Precision::FP32)},
        {"labels",
         std::make_shared<OutputBlobStub>(std::vector<size_t>{2, 1}, std::vector<int32_t>{0, 1}, Blob::Precision::I32)},
    };
    auto result = converter->convert(blobs);
    ASSERT_EQ(result.size(), 2);
    ASSERT_EQ(result[0].size(), 1);
    ASSERT_EQ(result[1].size(), 1);
    EXPECT_EQ(labelId(result[0][0][0]), 0);
    EXPECT_EQ(labelId(result[1][0][0]), 1);
    EXPECT_NEAR(detectionField(result[1][0][0], "x_min"), 0.5, 1e-6);
}

TEST(SsdOutputRoutingTest, ExtraModelOutputsAreIgnored) {
    ModelOutputsInfo outputs = duplicate_detections_info;
    outputs["feature_vector"] = {1, 8};
    auto converter = BlobToMetaConverter::create(makeSsdInitializer(outputs), ConverterType::TO_ROI, "ANY", "");
    OutputBlobs blobs = makeDuplicateDetections();
    blobs["feature_vector"] =
        std::make_shared<OutputBlobStub>(std::vector<size_t>{1, 8}, std::vector<float>(8, 0.f), Blob::Precision::FP32);
    EXPECT_EQ(converter->convert(blobs)[0].size(), 2);
}

TEST(SsdOutputRoutingTest, NormalizedBoxesAreClampedToImage) {
    ModelOutputsInfo outputs = {{"bboxes", {1, 1, 4}}, {"labels", {1, 1}}, {"scores", {1, 1}}};
    auto converter = BlobToMetaConverter::create(makeSsdInitializer(outputs), ConverterType::TO_ROI, "ANY", "");
    OutputBlobs blobs = {
        {"bboxes",
         std::make_shared<OutputBlobStub>(std::vector<size_t>{1, 1, 4},
                                          std::vector<float>{-0.004f, 0.1f, 1.0002f, 0.5f}, Blob::Precision::FP32)},
        {"labels",
         std::make_shared<OutputBlobStub>(std::vector<size_t>{1, 1}, std::vector<int64_t>{0}, Blob::Precision::I64)},
        {"scores",
         std::make_shared<OutputBlobStub>(std::vector<size_t>{1, 1}, std::vector<float>{0.9f}, Blob::Precision::FP32)},
    };
    auto result = converter->convert(blobs);
    ASSERT_EQ(result[0].size(), 1);
    EXPECT_NEAR(detectionField(result[0][0][0], "x_min"), 0.0, 1e-6);
    EXPECT_NEAR(detectionField(result[0][0][0], "x_max"), 1.0, 1e-6);
}

TEST(SsdOutputRoutingTest, NormalizedBoxesSkipNmsByDefault) {
    auto converter =
        BlobToMetaConverter::create(makeSsdInitializer(duplicate_detections_info), ConverterType::TO_ROI, "ANY", "");
    EXPECT_EQ(converter->convert(makeDuplicateDetections())[0].size(), 2);
}

TEST(SsdOutputRoutingTest, NmsExecuteFromModelInfoEnablesNms) {
    auto initializer = makeSsdInitializer(duplicate_detections_info);
    gst_structure_set(initializer.model_proc_output_info.get(), "nms_execute", G_TYPE_BOOLEAN, TRUE, nullptr);
    auto converter = BlobToMetaConverter::create(std::move(initializer), ConverterType::TO_ROI, "ANY", "");
    auto result = converter->convert(makeDuplicateDetections());
    ASSERT_EQ(result[0].size(), 1);
    EXPECT_NEAR(detectionField(result[0][0][0], "confidence"), 0.9, 1e-6);
}

TEST(SsdOutputRoutingTest, NonFiniteOutputsProduceNoDetections) {
    auto converter =
        BlobToMetaConverter::create(makeSsdInitializer(duplicate_detections_info), ConverterType::TO_ROI, "ANY", "");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    OutputBlobs blobs = makeDuplicateDetections();
    blobs["scores"] = std::make_shared<OutputBlobStub>(std::vector<size_t>{1, 2}, std::vector<float>{nan, nan},
                                                       Blob::Precision::FP32);
    EXPECT_TRUE(converter->convert(blobs)[0].empty());
}

TEST(SsdOutputRoutingTest, OutputsMatchingFormat) {
    EXPECT_TRUE(BoxesLabelsScoresConverter::isValidModelOutputs({{"boxes", {1, 10, 4}}}));
    EXPECT_TRUE(BoxesLabelsScoresConverter::isValidModelOutputs({{"boxes", {10, 5}}, {"labels", {10}}}));
    EXPECT_TRUE(BoxesLabelsScoresConverter::isValidModelOutputs({{"boxes", {1, 10, 5}}, {"labels", {1, 10}}}));
    EXPECT_TRUE(BoxesLabelsScoresConverter::isValidModelOutputs(duplicate_detections_info));

    EXPECT_FALSE(BoxesLabelsScoresConverter::isValidModelOutputs({{"boxes", {1, 10, 4}}, {"scores", {1, 10, 80}}}));
    EXPECT_FALSE(BoxesLabelsScoresConverter::isValidModelOutputs({{"boxes", {1, 10, 4}}, {"bboxes", {1, 10, 4}}}));
    EXPECT_FALSE(BoxesLabelsScoresConverter::isValidModelOutputs({{"boxes", {1, 10, 7}}}));
    EXPECT_FALSE(BoxesLabelsScoresConverter::isValidModelOutputs({{"detection_out", {1, 1, 100, 7}}}));
}