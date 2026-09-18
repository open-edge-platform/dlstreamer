/*******************************************************************************
 * Copyright (C) 2026 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

// Regression coverage for commit c0bf460a / PR #1047 ("fix wrong dimension ordering into
// cv::Size"): that commit assumed every instance-segmentation producer emits `dims` as
// [H, W], swapped the shared consumer (gvawatermarkimpl.cpp) accordingly, and thereby fixed
// mask_rcnn.cpp (the only [H,W] producer) while silently breaking yolo_v8, yolo_v26 and
// rfdetr (all of which emit [W,H]). These tests pin the actual, current dims convention
// produced by each converter so a future change to any one of them - or to the shared
// consumer - fails loudly instead of silently mis-rendering non-square masks.
//
// Masks are intentionally non-square (width != height) in every case: a transposed W/H
// cannot be detected with a square mask.

#include "common/post_processor/converters/to_roi/mask_rcnn.h"
#include "common/post_processor/converters/to_roi/rfdetr.h"
#include "common/post_processor/converters/to_roi/yolo_v26.h"
#include "common/post_processor/converters/to_roi/yolo_v8.h"

#include <dlstreamer/gst/videoanalytics/tensor.h>

#include <cstring>
#include <gmock/gmock.h>
#include <gst/gst.h>
#include <gtest/gtest.h>

using namespace post_processing;
using namespace InferenceBackend;

namespace {

// Minimal OutputBlob fake: wraps caller-provided dims/precision/data without touching a real
// inference backend, so converters can be exercised in complete isolation.
class FakeOutputBlob : public OutputBlob {
    std::vector<uint8_t> _data;
    std::vector<size_t> _dims;
    Blob::Precision _precision;

  public:
    template <typename T>
    FakeOutputBlob(std::vector<size_t> dims, const std::vector<T> &values, Blob::Precision precision)
        : _dims(std::move(dims)), _precision(precision) {
        _data.resize(values.size() * sizeof(T));
        std::memcpy(_data.data(), values.data(), _data.size());
    }

    const std::vector<size_t> &GetDims() const override {
        return _dims;
    }
    Layout GetLayout() const override {
        return Layout::ANY;
    }
    Precision GetPrecision() const override {
        return _precision;
    }
    const void *GetData() const override {
        return _data.data();
    }
};

BlobToMetaConverter::Initializer makeInitializer(size_t width, size_t height, GstStructure *model_proc_output_info,
                                                 std::vector<std::string> labels = {}) {
    BlobToMetaConverter::Initializer init;
    init.model_name = "segmentation_mask_dims_test";
    init.input_image_info.width = width;
    init.input_image_info.height = height;
    init.input_image_info.batch_size = 1;
    init.model_proc_output_info = GstStructureUniquePtr(model_proc_output_info, [](GstStructure *) {});
    init.labels = std::move(labels);
    return init;
}

// Finds the first non-detection tensor (i.e. the segmentation mask) among a box's tensors.
const GstStructure *findSegmentationTensor(const std::vector<GstStructure *> &box_tensors) {
    for (const GstStructure *s : box_tensors) {
        if (gst_structure_get_name_id(s) != g_quark_from_string("detection"))
            return s;
    }
    return nullptr;
}

} // namespace

struct SegmentationConverterDimsTest : public ::testing::Test {
    GstStructure *model_proc_output_info = nullptr;

    void SetUp() override {
        model_proc_output_info = gst_structure_new_empty("ANY");
    }

    void TearDown() override {
        if (model_proc_output_info)
            gst_structure_free(model_proc_output_info);
    }
};

// --- mask_rcnn: the original, historical [H,W] outlier ------------------------------------

TEST_F(SegmentationConverterDimsTest, MaskRCNNInstanceMaskDimsAreWidthHeight) {
    constexpr size_t mask_width = 9;
    constexpr size_t mask_height = 5;

    // three-tensor variant: boxes=[x1,y1,x2,y2,score], labels=[class_id], masks=[H,W]
    std::vector<float> boxes_data = {0.f, 0.f, 64.f, 32.f, 0.9f};
    std::vector<int64_t> labels_data = {0};
    std::vector<float> masks_data(mask_width * mask_height, 0.f);

    OutputBlobs blobs{
        {"boxes", std::make_shared<FakeOutputBlob>(std::vector<size_t>{1, 1, 5}, boxes_data, Blob::Precision::FP32)},
        {"labels", std::make_shared<FakeOutputBlob>(std::vector<size_t>{1, 1}, labels_data, Blob::Precision::I64)},
        {"masks", std::make_shared<FakeOutputBlob>(std::vector<size_t>{1, 1, mask_height, mask_width}, masks_data,
                                                   Blob::Precision::FP32)},
    };

    // labels_data[0]=0 maps to main_class = 0 + 1 = 1, so at least two labels are required.
    MaskRCNNConverter converter(makeInitializer(64, 32, model_proc_output_info, {"background", "person"}), 0.5, 0.4);
    TensorsTable result = converter.convert(blobs);

    ASSERT_FALSE(result.empty());
    ASSERT_FALSE(result[0].empty());
    const GstStructure *mask_tensor = findSegmentationTensor(result[0][0]);
    ASSERT_NE(mask_tensor, nullptr);

    GVA::Tensor tensor(const_cast<GstStructure *>(mask_tensor));
    EXPECT_EQ(tensor.dims(), (std::vector<guint>{mask_width, mask_height}))
        << "mask_rcnn must emit dims as [W, H], matching the convention used by every other "
           "instance-segmentation converter";
}

// --- yolo_v8_seg: always emitted [W,H]; broken by c0bf460a's consumer-side swap -----------

TEST_F(SegmentationConverterDimsTest, YoloV8SegInstanceMaskDimsAreWidthHeight) {
    constexpr size_t input_width = 64;
    constexpr size_t input_height = 32;
    constexpr size_t mask_width = input_width / 4;   // 16
    constexpr size_t mask_height = input_height / 4; // 8

    // boxes layout: [batch, object_size, num_boxes] with object_size = x,y,w,h,class_score,mask_score
    std::vector<float> boxes_data = {32.f, 16.f, 64.f, 32.f, 0.9f, 1.0f};
    std::vector<float> masks_data(mask_width * mask_height, 0.2f);

    OutputBlobs blobs{
        {"boxes", std::make_shared<FakeOutputBlob>(std::vector<size_t>{1, 6, 1}, boxes_data, Blob::Precision::FP32)},
        {"masks", std::make_shared<FakeOutputBlob>(std::vector<size_t>{1, 1, mask_height, mask_width}, masks_data,
                                                   Blob::Precision::FP32)},
    };

    YOLOv8SegConverter converter(makeInitializer(input_width, input_height, model_proc_output_info, {"object"}), 0.5,
                                 0.4);
    TensorsTable result = converter.convert(blobs);

    ASSERT_FALSE(result.empty());
    ASSERT_FALSE(result[0].empty());
    const GstStructure *mask_tensor = findSegmentationTensor(result[0][0]);
    ASSERT_NE(mask_tensor, nullptr);

    GVA::Tensor tensor(const_cast<GstStructure *>(mask_tensor));
    EXPECT_EQ(tensor.dims(), (std::vector<guint>{mask_width, mask_height}))
        << "yolo_v8_seg emits dims as [W, H]; the watermark consumer must agree with this, not "
           "with mask_rcnn's [H, W]";
}

// --- yolo_v26_seg: same [W,H] convention as yolo_v8_seg -----------------------------------

TEST_F(SegmentationConverterDimsTest, YoloV26SegInstanceMaskDimsAreWidthHeight) {
    constexpr size_t input_width = 64;
    constexpr size_t input_height = 32;
    constexpr size_t mask_width = input_width / 4;   // 16
    constexpr size_t mask_height = input_height / 4; // 8

    // boxes layout: [batch, num_boxes, object_size] with object_size = x1,y1,x2,y2,score,label,mask_score
    std::vector<float> boxes_data = {0.f, 0.f, 64.f, 32.f, 0.9f, 0.f, 1.0f};
    std::vector<float> masks_data(mask_width * mask_height, 0.2f);

    OutputBlobs blobs{
        {"boxes", std::make_shared<FakeOutputBlob>(std::vector<size_t>{1, 1, 7}, boxes_data, Blob::Precision::FP32)},
        {"masks", std::make_shared<FakeOutputBlob>(std::vector<size_t>{1, 1, mask_height, mask_width}, masks_data,
                                                   Blob::Precision::FP32)},
    };

    YOLOv26SegConverter converter(makeInitializer(input_width, input_height, model_proc_output_info, {"object"}), 0.5,
                                  0.4);
    TensorsTable result = converter.convert(blobs);

    ASSERT_FALSE(result.empty());
    ASSERT_FALSE(result[0].empty());
    const GstStructure *mask_tensor = findSegmentationTensor(result[0][0]);
    ASSERT_NE(mask_tensor, nullptr);

    GVA::Tensor tensor(const_cast<GstStructure *>(mask_tensor));
    EXPECT_EQ(tensor.dims(), (std::vector<guint>{mask_width, mask_height}))
        << "yolo_v26_seg emits dims as [W, H]; must stay consistent with yolo_v8_seg and rfdetr_seg";
}

// --- rfdetr_seg: also [W,H] ----------------------------------------------------------------

TEST_F(SegmentationConverterDimsTest, RfDetrSegInstanceMaskDimsAreWidthHeight) {
    constexpr size_t mask_width = 11;
    constexpr size_t mask_height = 7;
    constexpr size_t num_classes = 5; // must be > 4 so the converter recognizes the logits blob

    std::vector<float> logits_data(num_classes, -10.f);
    logits_data[0] = 10.f; // class 0 wins deterministically, sigmoid(10) far above threshold
    std::vector<float> boxes_data = {0.5f, 0.5f, 1.0f, 1.0f}; // full-frame box in cxcywh
    std::vector<float> masks_data(mask_width * mask_height, 0.2f);

    OutputBlobs blobs{
        {"logits",
         std::make_shared<FakeOutputBlob>(std::vector<size_t>{1, 1, num_classes}, logits_data, Blob::Precision::FP32)},
        {"boxes", std::make_shared<FakeOutputBlob>(std::vector<size_t>{1, 1, 4}, boxes_data, Blob::Precision::FP32)},
        {"masks", std::make_shared<FakeOutputBlob>(std::vector<size_t>{1, 1, mask_height, mask_width}, masks_data,
                                                   Blob::Precision::FP32)},
    };

    std::vector<std::string> labels(num_classes, "object");
    RFDETRSegConverter converter(makeInitializer(100, 80, model_proc_output_info, labels), 0.5);
    TensorsTable result = converter.convert(blobs);

    ASSERT_FALSE(result.empty());
    ASSERT_FALSE(result[0].empty());
    const GstStructure *mask_tensor = findSegmentationTensor(result[0][0]);
    ASSERT_NE(mask_tensor, nullptr);

    GVA::Tensor tensor(const_cast<GstStructure *>(mask_tensor));
    EXPECT_EQ(tensor.dims(), (std::vector<guint>{mask_width, mask_height}))
        << "rfdetr_seg emits dims as [W, H]; must stay consistent with yolo_v8_seg/yolo_v26_seg";
}
