/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Self-contained unit test for the zero-shot OpenCLIP converter. It writes a tiny safetensors
// fixture from C++ (no PyTorch / no Python at build or run time), constructs the converter, feeds a
// mock image-embedding blob, and checks ranking, logit_scale calibration and the unknown-threshold.

#include "common/post_processor/converters/to_tensor/zeroshot_openclip.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace InferenceBackend;
using namespace post_processing;

namespace {

// Minimal F32 safetensors writer (little-endian host). Produces a single 2-D tensor named
// "embeddings" plus optional string metadata, matching what the sample tooling emits.
void write_safetensors_f32(const std::string &path, const std::vector<std::vector<float>> &rows,
                           const std::string &metadata_json = std::string()) {
    const std::size_t r = rows.size();
    const std::size_t c = rows.empty() ? 0 : rows.front().size();
    const std::size_t nbytes = r * c * sizeof(float);

    std::string header = "{\"embeddings\":{\"dtype\":\"F32\",\"shape\":[" + std::to_string(r) + "," +
                         std::to_string(c) + "],\"data_offsets\":[0," + std::to_string(nbytes) + "]}";
    if (!metadata_json.empty())
        header += ",\"__metadata__\":" + metadata_json;
    header += "}";

    std::ofstream f(path, std::ios::binary);
    uint64_t header_len = header.size();
    unsigned char lenbuf[8];
    for (int i = 0; i < 8; ++i)
        lenbuf[i] = static_cast<unsigned char>((header_len >> (8 * i)) & 0xFF);
    f.write(reinterpret_cast<const char *>(lenbuf), 8);
    f.write(header.data(), static_cast<std::streamsize>(header.size()));
    for (const auto &row : rows)
        f.write(reinterpret_cast<const char *>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(float)));
}

// Mock blob carrying a single image embedding [1, dim] as FP32.
class TestEmbeddingBlob : public OutputBlob {
    std::vector<float> _data;
    std::vector<size_t> _dims;

  public:
    TestEmbeddingBlob(std::vector<float> data, std::vector<size_t> dims)
        : _data(std::move(data)), _dims(std::move(dims)) {
    }
    const std::vector<size_t> &GetDims() const override {
        return _dims;
    }
    const void *GetData() const override {
        return _data.data();
    }
    Layout GetLayout() const override {
        return Layout::NC;
    }
    Precision GetPrecision() const override {
        return Precision::FP32;
    }
};

struct ZeroShotOpenCLIPConverterTest : public testing::Test {
  protected:
    std::string _embeddings_path = "zeroshot_openclip_test_embeddings.safetensors";
    std::vector<size_t> _output_dims{1, 4};
    GstStructure *_gst_structure{nullptr};

    BlobToMetaConverter::Initializer CreateInitializer(uint32_t topk) {
        BlobToMetaConverter::Initializer initializer;
        initializer.model_name = "zeroshot_openclip_test";
        initializer.outputs_info = {{"image_embedding", _output_dims}};
        initializer.input_image_info.batch_size = 1;
        initializer.labels = {"a", "b", "c"};
        initializer.zeroshot_embeddings_file = _embeddings_path;
        initializer.zeroshot_topk = topk;
        initializer.model_proc_output_info = GstStructureUniquePtr(_gst_structure, [](auto) {});
        return initializer;
    }

    void SetUp() override {
        _gst_structure = gst_structure_new_empty("ANY");
        // Orthonormal class prototypes for a, b, c; logit_scale in metadata (calibrated softmax).
        write_safetensors_f32(_embeddings_path, {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}},
                              "{\"logit_scale\":\"100.0\"}");
    }

    void TearDown() override {
        if (_gst_structure)
            gst_structure_free(_gst_structure);
        _gst_structure = nullptr;
        std::remove(_embeddings_path.c_str());
    }

    OutputBlobs MakeBlob(std::vector<float> embedding) {
        auto blob = std::make_shared<TestEmbeddingBlob>(std::move(embedding), _output_dims);
        return OutputBlobs{{"image_embedding", blob}};
    }
};

TEST_F(ZeroShotOpenCLIPConverterTest, ConverterName) {
    ASSERT_EQ(ZeroShotOpenCLIPConverter::getName(), "zeroshot_openclip");
}

TEST_F(ZeroShotOpenCLIPConverterTest, MissingEmbeddingsFileThrows) {
    auto init = CreateInitializer(1);
    init.zeroshot_embeddings_file.clear();
    EXPECT_THROW(ZeroShotOpenCLIPConverter{std::move(init)}, std::invalid_argument);
}

TEST_F(ZeroShotOpenCLIPConverterTest, RanksClosestClassFirstAndCalibrates) {
    ZeroShotOpenCLIPConverter converter(CreateInitializer(3));
    auto blobs = MakeBlob({0.1f, 0.9f, 0.0f, 0.0f}); // closest to class "b"
    TensorsTable result = converter.convert(blobs);

    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result[0].size(), 3u); // top-k = 3

    const GstStructure *top = result[0][0][0];
    EXPECT_STREQ(gst_structure_get_string(top, "label"), "b");
    int label_id = -100, rank = 0;
    gst_structure_get_int(top, "label_id", &label_id);
    gst_structure_get_int(top, "rank", &rank);
    EXPECT_EQ(label_id, 1);
    EXPECT_EQ(rank, 1);

    gboolean zs_mode = FALSE, zs_unknown = TRUE;
    gst_structure_get_boolean(top, "zs_mode", &zs_mode);
    gst_structure_get_boolean(top, "zs_unknown", &zs_unknown);
    EXPECT_TRUE(zs_mode);
    EXPECT_FALSE(zs_unknown);

    // logit_scale=100 makes the softmax peaked, so the top-1 confidence is high and beats rank-2.
    double top_conf = 0.0, second_conf = 1.0;
    gst_structure_get_double(top, "confidence", &top_conf);
    gst_structure_get_double(result[0][1][0], "confidence", &second_conf);
    EXPECT_GT(top_conf, 0.5);
    EXPECT_GT(top_conf, second_conf);
}

TEST_F(ZeroShotOpenCLIPConverterTest, BelowUnknownThresholdIsUnknown) {
    gst_structure_set(_gst_structure, "unknown_threshold", G_TYPE_DOUBLE, 0.95, NULL);
    ZeroShotOpenCLIPConverter converter(CreateInitializer(3));
    auto blobs = MakeBlob({0.6f, 0.6f, 0.5f, 0.0f}); // max cosine ~0.61 < 0.95
    TensorsTable result = converter.convert(blobs);

    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result[0].size(), 1u); // single "unknown" result instead of the ranking

    const GstStructure *top = result[0][0][0];
    EXPECT_STREQ(gst_structure_get_string(top, "label"), "unknown");
    int label_id = -100;
    gst_structure_get_int(top, "label_id", &label_id);
    EXPECT_EQ(label_id, -1);
    gboolean zs_unknown = FALSE;
    gst_structure_get_boolean(top, "zs_unknown", &zs_unknown);
    EXPECT_TRUE(zs_unknown);
}

} // namespace
